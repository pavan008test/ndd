#!/bin/bash
# systemdslotchangeprodfinal.sh -- Safe slot repair (STEP VERIFICATION)
# FIXED: ALWAYS RETURN TO SLOT A ON ANY FAILURE + sync after abctl
# written by Origanti Lalita

LOG="/data/log_slot_switch.log"
FLAG="/data/slot_switch.flag"
MOUNT_POINT="/data/inactive_slot_mnt"
ROOTA_DEV="/dev/mmcblk0p14"
ROOTB_DEV="/dev/mmcblk0p15"
UPLOAD_DIR="/data/nd_files/log/circ_buff"
SCRIPT_NAME=$(basename "$0")
SERVICE_NAME="slot-fix.service"
EARLY_SUCCESS_SERVICE="early-slot-success.service"
DONE="/data/done"
REBOOT_MODE="true"

timestamp() { date '+%Y-%m-%d %H:%M:%S'; }
log() { echo "[$(timestamp)] $*" | tee -a "$LOG"; }
check_step() {
  local step="$1" rc="$2"
  if [ "$rc" -eq 0 ]; then
    log "$step SUCCESS"
  else
    log "$step FAILED (rc=$rc)"
    return 1
  fi
}

# FORCE RETURN TO SLOT A - used by ALL error paths
force_back_to_a() {
  log "FORCE SWITCHING BACK TO SLOT A (unverified rootfs) - Reason: $*"
  rm -f "$FLAG"*
  sync
  if run_cmd "abctl --set_active 0"; then
    sync
  else
    log "FATAL: abctl --set_active 0 failed - STAYING ON SLOT B"
    exit 1
  fi
  log "FLAG destroyed. Rebooting to Slot A..."
  cli_mgr <<EOF
msp qcs_reboot
exit
EOF
  exit 0
}

run_cmd() {
  local cmd="$1"
  echo "----- CMD: $cmd -----" >> "$LOG"
  eval "$cmd" >> "$LOG" 2>&1
  local rc=$?
  echo "----- RC=$rc CMD: $cmd -----" >> "$LOG"
  check_step "CMD: $cmd" "$rc"
  echo "" >> "$LOG"
  return $rc
}

safe_unmount() {
  local mp="$1"
  log "safe_unmount: $mp"
  cd / || true
  run_cmd "sync" || return 1

  for attempt in 1 2 3; do
    log "Attempt $attempt/3: umount $mp"
    if umount "$mp" >> "$LOG" 2>&1; then
      log "umount succeeded"
      return 0
    fi
    fuser -km "$mp" >> "$LOG" 2>&1 || true
    sleep 1
  done

  umount -f "$mp" >> "$LOG" 2>&1 || umount -l "$mp" >> "$LOG" 2>&1 || {
    log "FATAL: unmount failed"
    return 1
  }
  log "umount forced"
  return 0
}

# REBOOT MODE ARG PARSING
if [ "$1" = "false" ]; then
    REBOOT_MODE="false"   # Circular buffer/service handles reboot
    log "Running in service-managed mode - service will handle reboot decisions"
else
    REBOOT_MODE="true"
    log "Running in script-managed mode - script will handle reboot directly"
fi

# --- Start ---
touch "$LOG" 2>/dev/null || { echo "Cannot write to $LOG"; exit 1; }
log "===== START $SCRIPT_NAME ====="

CURRENT_ROOT=$(mount | awk '$3=="/" {print $1}' || true)
log "Detected root: $CURRENT_ROOT"

ACTIVE_SLOT_RAW=$(abctl --boot_slot 2>/dev/null || echo "unknown")
if echo "$ACTIVE_SLOT_RAW" | grep -q "_a"; then ACTIVE_SLOT="a"
elif echo "$ACTIVE_SLOT_RAW" | grep -q "_b"; then ACTIVE_SLOT="b"
else ACTIVE_SLOT="unknown"; fi
log "Slot: $ACTIVE_SLOT_RAW → $ACTIVE_SLOT"

# ================================================================
# STAGE 1: PREPARE FROM SLOT A → REBOOT B
# ================================================================
if [ ! -f "$FLAG" ]; then
  log "=== STAGE 1: From Slot A → Prepare Slot B ==="

  [ "$ACTIVE_SLOT" = "a" ] || { log "MUST RUN FROM SLOT A"; exit 1; }
  # STEP 2: Mount Slot B
  log "STEP 2: Mounting Slot B..."
  run_cmd "mkdir -p $MOUNT_POINT" || exit 1
  run_cmd "mount -t ext4 -o defaults $ROOTB_DEV $MOUNT_POINT" || exit 1
  log "STEP 2: Slot B mounted"

# STEP 3: Services on Slot B (EARLY + SLOT-FIX)
log "STEP 3: Creating services on Slot B..."
run_cmd "mkdir -p $MOUNT_POINT/etc/systemd/system/{sysinit,multi-user}.target.wants" || exit 1

# EARLY SUCCESS SERVICE on Slot B
EARLY_PATH="$MOUNT_POINT/etc/systemd/system/$EARLY_SUCCESS_SERVICE"
if [ ! -f "$EARLY_PATH" ]; then
    log "EARLY service missing → creating on Slot B"
    cat >"$EARLY_PATH" <<'EOL'
[Unit]
Description=EARLY slot success (sysinit)
DefaultDependencies=no
After=systemd-remount-fs.service
Before=sysinit.target

[Service]
Type=oneshot
ExecStart=/sbin/abctl --set_success
TimeoutStartSec=5s
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
EOL
    log "EARLY service created"
else
    log "EARLY service already exists on Slot B → skipping creation"
fi

# SLOT-FIX SERVICE on Slot B
SLOTFIX_PATH="$MOUNT_POINT/etc/systemd/system/$SERVICE_NAME"
if [ ! -f "$SLOTFIX_PATH" ]; then
    log "SLOT-FIX service missing → creating on Slot B"
    cat >"$SLOTFIX_PATH" <<EOL
[Unit]
Description=Slot repair script
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/bash /data/$SCRIPT_NAME
StandardOutput=append:$LOG
StandardError=append:$LOG

[Install]
WantedBy=multi-user.target
EOL
    log "SLOT-FIX service created"
else
    log "SLOT-FIX service already exists on Slot B → skipping creation"
fi

# Ensure symlinks exist (enable services)
log "Enabling services on Slot B via symlinks..."
run_cmd "ln -sf ../$EARLY_SUCCESS_SERVICE $MOUNT_POINT/etc/systemd/system/sysinit.target.wants/$EARLY_SUCCESS_SERVICE" || exit 1
run_cmd "ln -sf ../$SERVICE_NAME $MOUNT_POINT/etc/systemd/system/multi-user.target.wants/$SERVICE_NAME" || exit 1
run_cmd "sync" || exit 1
log "STEP 3: Slot B services ready"

  # STEP 4: Flag + switch
  log "STEP 4: Setting flag + switching..."
  echo "after_reboot" > "$FLAG"
  sync || exit 1
  run_cmd "abctl --set_active 1" || exit 1
  sync
  safe_unmount "$MOUNT_POINT" || log "Unmount warning (continuing)"
  log "STEP 4: Flag + switch"

  # REBOOT MODE HANDLING
  if [ "$REBOOT_MODE" = "false" ]; then
      log "Service-managed mode: Stage 1 completed successfully - returning control to service"
      log "Service should handle reboot to Slot B"
      exit 10  # Service will handle reboot to Slot B
  else
      log "Script-managed mode: Stage 1 completed, rebooting to Slot B directly"
      log "ALL STEPS VERIFIED → Rebooting to Slot B"
      cli_mgr <<EOF
msp qcs_reboot
exit
EOF
      exit 0
  fi
fi  # CLOSES: if [ ! -f "$FLAG" ]

# ================================================================
# STAGE 2: REPAIR FROM SLOT B → BACK TO A
# ================================================================
log "=== STAGE 2: Repair from Slot B → Verify → Back to A ==="

[ "$CURRENT_ROOT" = "$ROOTB_DEV" ] || force_back_to_a "Not on Slot B ($CURRENT_ROOT)"

# STEP 1: MSP alive
log "STEP 1: MSP QCS alive..."
if ! cli_mgr <<EOF
msp qcs_alive
exit
EOF
then
  force_back_to_a "MSP qcs_alive failed"
fi
log "STEP 1: MSP alive"

# STEP 2: FSCK Slot A (3 tries max)
log "STEP 2: fsck Slot A ($ROOTA_DEV)..."
success=0
for attempt in 1 2 3; do
  log "fsck attempt $attempt/3"
  fsck -f -yv "$ROOTA_DEV" >> "$LOG" 2>&1
  rc=$?
  log "fsck rc=$rc"
  if [ $rc -le 2 ]; then
    success=1
    log "fsck success (rc=$rc)"
    break
  fi
  sleep 3
done

if [ $success -eq 0 ]; then
  force_back_to_a "FSCK failed after 3 attempts"
fi
log "STEP 2: FSCK complete"

# STEP 3: Mount + RW test Slot A + ENABLE PRE-DEPLOYED EARLY service
log "STEP 3: Mount + RW test + enable EARLY service on Slot A..."
run_cmd "mkdir -p $MOUNT_POINT" || force_back_to_a "mkdir $MOUNT_POINT failed"
run_cmd "mount -t ext4 -o defaults,rw $ROOTA_DEV $MOUNT_POINT" || force_back_to_a "mount $ROOTA_DEV failed"

TEST_FILE="$MOUNT_POINT/test_$(date +%s)"

# 3A: RW TEST (touch)
if ! touch "$TEST_FILE" 2>/dev/null; then
  log "RW test FAILED (touch) → forcing back to Slot A"
  safe_unmount "$MOUNT_POINT" || true
  force_back_to_a "RW test (touch) failed on Slot A"
fi
# 3B: CREATE (if missing) + ENABLE PRE-DEPLOYED EARLY SERVICE (using chroot)
log "STEP 3B: Ensuring EARLY service on Slot A (create if missing + enable)..."

EARLY_PATH="$MOUNT_POINT/etc/systemd/system/$EARLY_SUCCESS_SERVICE"
if [ ! -f "$EARLY_PATH" ]; then
    log "EARLY service missing → creating on Slot A"
    cat >"$EARLY_PATH" <<'EOL'
[Unit]
Description=EARLY slot success (sysinit)
DefaultDependencies=no
After=systemd-remount-fs.service
Before=sysinit.target

[Service]
Type=oneshot
ExecStart=/sbin/abctl --set_success
TimeoutStartSec=5s
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
EOL
    log "EARLY service created on Slot A"
else
    log "EARLY service already exists on Slot A"
fi

# ALWAYS ENABLE (create symlink via chroot)
run_cmd "chroot $MOUNT_POINT systemctl enable $EARLY_SUCCESS_SERVICE" || force_back_to_a "chroot systemctl enable failed on Slot A"
log "STEP 3B: EARLY service ENABLED on Slot A"


# 3C: CLEAN RW TEST FILE
if ! rm -f "$TEST_FILE" 2>/dev/null; then
  log "RW test FAILED (rm) → forcing back to Slot A"
  safe_unmount "$MOUNT_POINT" || true
  force_back_to_a "RW test (rm) failed on Slot A"
fi

safe_unmount "$MOUNT_POINT" || force_back_to_a "STEP 3: safe_unmount failed"
log "STEP 3: Slot A RW verified + EARLY service ENABLED"


# ONLY HERE we "mark success"
echo "DONE" > "$DONE"
sync

# STEP 4: Cleanup services + logs (on Slot B)
log "STEP 4: Cleanup services + logs..."
run_cmd "systemctl disable $SERVICE_NAME" || true
run_cmd "mkdir -p $UPLOAD_DIR" || true
run_cmd "cp -f $LOG $UPLOAD_DIR/slot_switch.log" || true
log "STEP 4: Cleanup complete"

# STEP 5: Switch back to A + DESTROY FLAG
log "STEP 5: Switch to Slot A + Flag cleanup..."
run_cmd "abctl --set_active 0" || force_back_to_a "STEP 5: abctl --set_active 0 failed"

log "DESTROYING FLAG..."
rm -f "$FLAG"*
sync; sleep 1; sync
if [ -f "$FLAG" ] 2>/dev/null; then
  force_back_to_a "FATAL: Flag persists after cleanup"
fi

sync
log "STEP 5: Flag DESTROYED $(find /data -name 'slot_switch.flag*' 2>/dev/null || echo 'NO FILES')"

log "ALL 5 STEPS VERIFIED SUCCESSFUL → Reboot to Slot A"
cli_mgr <<EOF
msp qcs_reboot
exit
EOF

