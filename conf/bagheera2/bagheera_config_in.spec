{
    "sdcard": {
        "sizeForCircularBuffer": {
            "type": "int",
            "value": 90,
            "desc": "%of SDcard allocated for circular buffer",
            "rules": {
                "min": 80,
                "max": 100
            }
        },
        "deleteUnknownFile": {
            "type": "bool",
            "value": false,
            "desc": "true: delete file if its file type in add_file_db is not known, false: Don't delete",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "use_extended_attr": {
            "type": "bool",
            "value": true,
            "desc": "true:use extended attributes enabled, false:use extended attributes disabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "payload_dump_interval": {
            "type": "int",
            "value": 0,
            "desc": "value is describing for how long to dump the payload in hours",
            "rules": {
                "min" : 0,
                "max" : 24
            }
        }
    },
    "drp": {
        "enabled": {
            "type": "int",
            "value": 0,
            "desc": "0 - disabled, 1 - enabled",
            "rules": {
                "min": 0,
                "max": 1
            }
        },
        "clock_hours": {
            "type": "int",
            "value": 72,
            "desc": "Default 72 (3 days). Valid: 72,120,168,240,720",
            "rules": {
                "allowed_values": [
                    72,
                    120,
                    168,
                    240,
                    720
                ]
            }
        }
    },
    "driverlogin": {
        "enabled": {
            "type": "bool",
            "value": true,
            "desc": "feature enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "login_speed": {
            "type": "float",
            "value": 15,
            "desc": "legacy login speed threshold; 2.0 uses for reminders",
            "rules": {
                "min": 0,
                "max": 200
            }
        },
        "login_time": {
            "type": "int",
            "value": 10,
            "desc": "time (secs) to maintain speed for login",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "idle_speed": {
            "type": "int",
            "value": 0,
            "desc": "For 2.0, on out of idle, login kicks in",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "idle_time": {
            "type": "int",
            "value": 300,
            "desc": "idle time threshold (secs)",
            "rules": {
                "min": 0,
                "max": 10800
            }
        },
        "bt_max_retry": {
            "type": "int",
            "value": 5,
            "desc": "BT scan retry count",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "bt_scan_retry_vp_enabled": {
            "type": "int",
            "value": 5,
            "desc": "default scan retry count visionpro enabled",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "coexist_with_visionpro": {
            "type": "bool",
            "value": true,
            "desc": "coexist with visionpro",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "curl_timeout": {
            "type": "int",
            "value": 60,
            "desc": "curl timeout posting driver login (secs)",
            "rules": {
                "min": 1,
                "max": 600
            }
        },
        "cloud_post_retry_interval": {
            "type": "int",
            "value": 10,
            "desc": "retry interval cloud post (mins)",
            "rules": {
                "min": 1,
                "max": 1440
            }
        },
        "driveri_app_login": {
            "type": "bool",
            "value": false,
            "desc": "Driveri App login/logout",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "enable_audio_reminders": {
            "type": "bool",
            "value": false,
            "desc": "audio prompt for login / pairing",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "requires_audio_reminders_on_ignition": {
            "type": "bool",
            "value": true,
            "desc": "message at power on",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "audio_interval": {
            "type": "int",
            "value": 60,
            "desc": "interval between audio plays (multiple of 30)",
            "rules": {
                "min": 30,
                "max": 3600
            }
        },
        "audio_max_count": {
            "type": "int",
            "value": 3,
            "desc": "max plays until login",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "audio_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/Please_stop_and_log_in.wav",
            "desc": "audio file for login reminders",
            "rules": {
                "max_length": 5000
            }
        },
        "force_first_audio_play": {
            "type": "int",
            "value": 0,
            "desc": "first audio even without internet",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "first_audio_play_interval": {
            "type": "int",
            "value": 150,
            "desc": "delay before first audio (secs) considering internet connectivity",
            "rules": {
                "min": 0,
                "max": 3600
            }
        }
    },
    "driverlogin_v2": {
        "enabled": {
            "type": "bool",
            "value": false,
            "desc": "v2 driver login enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "login_speed": {
            "type": "float",
            "value": 0,
            "desc": "speed used to play reminders",
            "rules": {
                "min": 0,
                "max": 200
            }
        },
        "login_time": {
            "type": "int",
            "value": 10,
            "desc": "maintain speed time (secs)",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "idle_speed": {
            "type": "int",
            "value": 0,
            "desc": "on out of idle login kicks",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "idle_time": {
            "type": "int",
            "value": 0,
            "desc": "idle time threshold (secs)",
            "rules": {
                "min": 0,
                "max": 10800
            }
        },
        "enable_audio_reminders": {
            "type": "bool",
            "value": false,
            "desc": "audio prompt for login",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "requires_audio_reminders_on_ignition": {
            "type": "bool",
            "value": true,
            "desc": "message at power on",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "audio_interval": {
            "type": "int",
            "value": 60,
            "desc": "interval between audio (multiple 30)",
            "rules": {
                "min": 30,
                "max": 3600
            }
        },
        "audio_max_count": {
            "type": "int",
            "value": 3,
            "desc": "max plays until login",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "force_first_audio_play": {
            "type": "int",
            "value": 0,
            "desc": "first audio no internet",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "first_audio_play_interval": {
            "type": "int",
            "value": 150,
            "desc": "delay first audio (secs) considering internet connectivity",
            "rules": {
                "min": 0,
                "max": 3600
            }
        },
        "login_reminder_audio_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/Please_stop_and_log_in.wav",
            "desc": "audio file for driver login reminders",
            "rules": {
                "max_length": 5000
            }
        },
        "qr_enabled": {
            "type": "bool",
            "value": false,
            "desc": "enable QR based login",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "max_qr_scan_duration": {
            "type": "int",
            "value": 3600,
            "desc": "maximum QR scan duration per session (secs)",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "enable_invalid_qr_image_dump": {
            "type": "bool",
            "value": false,
            "desc": "dump invalid QR image frames when enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "qr_login_success_audio_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/Thank_you_for_logging_in.wav",
            "desc": "audio played on successful QR login",
            "rules": {
                "max_length": 5000
            }
        },
        "qr_login_tags": {
            "type": "string",
            "value": "Driverid",
            "desc": "comma separated QR tags to match",
            "rules": {
                "max_length": 1024
            }
        },
        "idle_speed_qr_resume_threshold": {
            "type": "int",
            "value": 2,
            "desc": "speed (mph) below which QR scan resumes",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "idle_speed_qr_resume_duration_secs": {
            "type": "int",
            "value": 5,
            "desc": "duration speed must stay below threshold to resume scan (secs)",
            "rules": {
                "min": 0,
                "max": 600
            }
        }
    },
    "driverlogin_fr": {
        "enabled": {
            "type": "bool",
            "value": true,
            "desc": "Fr driver login enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "idle_speed": {
            "type": "int",
            "value": 0,
            "desc": "idle speed threshold",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "idle_time": {
            "type": "int",
            "value": 300,
            "desc": "idle time threshold (secs)",
            "rules": {
                "min": 0,
                "max": 10800
            }
        }
    },
    "privacy_mode": {
        "outward": {
            "type": "string",
            "value": "false",
            "desc": "flag for enabling outward camera privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "inward": {
            "type": "string",
            "value": "true",
            "desc": "flag for enabling inward camera privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "left": {
            "type": "string",
            "value": "false",
            "desc": "flag for enabling left camera privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "right": {
            "type": "string",
            "value": "false",
            "desc": "flag for enabling right camera privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "audio": {
            "type": "string",
            "value": "true",
            "desc": "driveri audio privacy(if it is true audio recording will be disabled)",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "ext_cam": {
            "type": "string",
            "value": "false",
            "desc": "all external cameras privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "ext_cam_audio": {
            "type": "string",
            "value": "true",
            "desc": "external camera audio privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "save_user_alert_video": {
            "type": "string",
            "value": "false",
            "desc": "if this is true, driver initiated alert will upload irrespective of privacy(applicable only to inward)",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "gps": {
            "type": "string",
            "value": "false",
            "desc": "gps tracking will be disabled if it is true",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "off_duty_mode": {
            "type": "string",
            "value": "false",
            "desc": "personal privacy feature",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "inward_led_color": {
            "type": "string",
            "value": "red",
            "desc": "decides inward led color(can be red, green or purple)",
            "rules": {
                "allowed_values": [
                    "red",
                    "green",
                    "purple"
                ]
            }
        },
        "default_privacy_v3": {
            "type": "string",
            "value": "true",
            "desc": "Default speed privacy status used by ndcentral until BTFV service enables/disables privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "enhanced_privacy": {
            "type": "string",
            "value": "false",
            "desc": "if this is true don't save inward video even privacy is off",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "privacymode_led": {
            "type": "string",
            "value": "true",
            "desc": "to change led colour in privacy",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        }
    },
    "privacy_mode_activate": {
        "speed_based": {
            "type": "string",
            "value": "true",
            "desc": "Decides if privacy status changes based on speed or not",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "enable_speed": {
            "type": "int",
            "value": 0,
            "desc": "speed threshold in mph after which privacy will be activated(if speed_based is true)",
            "rules": {
                "min": 0,
                "max": 180
            }
        },
        "enable_time": {
            "type": "int",
            "value": 30,
            "desc": "speed threshold along with this much time in sec, after which privacy will be activated(if speed_based is true)",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "ignition_based": {
            "type": "string",
            "value": "true",
            "desc": "if true, enters to privacy on ign low comes out on ign high",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "button_long_press": {
            "type": "string",
            "value": "false",
            "desc": "button long press required for privacy ON, in ignition off condition",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "long_press_duration_ms": {
            "type": "int",
            "value": 5000,
            "desc": "button long press duration in msec",
            "rules": {
                "min": 3000,
                "max": 7000
            }
        },
        "post_ignition_off_duration": {
            "type": "int",
            "value": 0,
            "desc": "after this much duration in sec of ignition off, privacy will be activated(if ignition_based is true)",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "transition_audio_alert": {
            "type": "string",
            "value": "false",
            "desc": "play audio disabled->enabled",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "transition_audio_alert_file_regular": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_activated_en_f.wav",
            "desc": "path of transition audio file to be played in case of regular privacy",
            "rules": {
                "max_length": 5000
            }
        },
        "transition_audio_alert_file_enhanced": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/enhanced_privacy_mode_with_inward_camera_in_local_mode_is_activated_en_f.wav",
            "desc": "path of transition audio file to be played in case of enhanced privacy",
            "rules": {
                "max_length": 5000
            }
        },
        "transition_audio_alert_file_offduty": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_activated_en_f.wav",
            "desc": "path of transition audio file to be played in case of off duty mode",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "privacy_mode_deactivate": {
        "speed_based": {
            "type": "string",
            "value": "true",
            "desc": "Decides if privacy status changes based on speed or not",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "disable_speed": {
            "type": "int",
            "value": 5,
            "desc": "speed threshold in mph after which privacy will be deactivated(if speed_based is true)",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "disable_time": {
            "type": "int",
            "value": 5,
            "desc": "speed threshold along with this much time in sec, after which privacy will be deactivated(if speed_based is true)",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "ignition_based": {
            "type": "string",
            "value": "true",
            "desc": "if true, enters to privacy on ign low comes out on ign high",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "post_ignition_on_duration": {
            "type": "int",
            "value": 0,
            "desc": "after this much duration in sec of ignition on, privacy will be deactivated(if ignition_based is true)",
            "rules": {
                "min": 0,
                "max": 600
            }
        },
        "transition_audio_alert": {
            "type": "string",
            "value": "false",
            "desc": "if this is true, audio alert will be played when privacy transition happens from enable to disable",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "transition_audio_alert_file_regular": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_deactivated_en_f.wav",
            "desc": "path of transition (enable to disable) audio file to be played in case of regular privacy",
            "rules": {
                "max_length": 5000
            }
        },
        "transition_audio_alert_file_offduty": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_deactivated_en_f.wav",
            "desc": "path of transition (enable to disable) audio file to be played in case of off duty mode",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "upload_video": {
        "outward": {
            "type": "string",
            "value": "true",
            "desc": "upload of outward camera to IDMS will be disabled if this is false",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "inward": {
            "type": "string",
            "value": "false",
            "desc": "upload of inward camera to IDMS will be disabled if this is false",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "left": {
            "type": "string",
            "value": "true",
            "desc": "upload of left camera to IDMS will be disabled if this is false",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "right": {
            "type": "string",
            "value": "true",
            "desc": "upload of right camera to IDMS will be disabled if this is false",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "ext_cam": {
            "type": "string",
            "value": "true",
            "desc": "upload of dms camera to IDMS will be disabled if this is false",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        }
    },
    "engine_idle": {
        "enabled": {
            "type": "string",
            "value": "false",
            "desc": "disabling engine idle by default from 0.4.6 (Not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "engine_idle_enable_speed": {
            "type": "int",
            "value": 0,
            "desc": "Speed below which you want to go to idle (Not using)",
            "rules": {
                "min": 0,
                "max": 50
            }
        },
        "engine_idle_enable_time": {
            "type": "int",
            "value": 36000,
            "desc": "Time in secs after which you want to go to idle (Not using)",
            "rules": {
                "allowed_values": [
                    36000
                ]
            }
        }
    },
    "camera": {
        "front": {
            "type": "string",
            "value": "enable",
            "desc": "to enable/disable outward camera",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "back": {
            "type": "string",
            "value": "disable",
            "desc": "to enable/disable inward camera",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "left": {
            "type": "string",
            "value": "disable",
            "desc": "to enable/disable left camera",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "right": {
            "type": "string",
            "value": "disable",
            "desc": "to enable/disable right camera",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "side_cam_disable_threshold_time": {
            "type": "int",
            "value": 7200000,
            "desc": "in milliseconds if a side camera crashes for max_cam_crash_count number of times within this interval, it will be disabled for the boot cycle",
            "rules": {
            "min": 0,
            "max": 86400000
            }
        },
        "codecType": {
            "type": "string",
            "value": "hevc",
            "desc": "type of encoding (Not using)",
            "rules": {
                "allowed_values": [
                    "hevc"
                ]
            }
        },
        "use_nvmm": {
            "type": "string",
            "value": "true",
            "desc": "(Not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "nv_queue_size": {
            "type": "int",
            "value": 25,
            "desc": "NV buffer size for nvcamersrc gst element's queue-size property (Not using)",
            "rules": {
                "allowed_values": [
                    25
                ]
            }
        },
        "is_drop_only": {
            "type": "string",
            "value": "true",
            "desc": "(Not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "sync_files": {
            "type": "string",
            "value": "true",
            "desc": "whether to enable file sync feature or not",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "sync_freq_in_millisecs": {
            "type": "int",
            "value": 1000,
            "desc": "frequency at which video files should be synced (to be provided in milliseconds",
            "rules": {
                "min": 500,
                "max": 2000
            }
        },
        "audio_enable": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable audio recording",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "audio_encryption": {
            "type": "string",
            "value": "true",
            "desc": "flag to encrypt audio file",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "video_encryption": {
            "type": "string",
            "value": "true",
            "desc": "flag to encrypt video file",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "max_cam_crash_count": {
            "type": "int",
            "value": 5,
            "desc": "maximum crashes allowed for\nleft/right camera in a single boot cycle. After this camera will be disabled for the boot cycle",
            "rules": {
                "min": 1,
                "max": 100
            }
        },
        "copy_hd_files": {
            "type": "string",
            "value": "true",
            "desc": "flag to check if hd files to be moved to sd_card or not (Not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "trace_logs": {
            "type": "string",
            "value": "false",
            "desc": "Flag to capture / delete trace logs",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "usev4l2src": {
            "type": "string",
            "value": "false",
            "desc": "deciedes whether to use v4l2src or nvv4l2src ; if false uses nvv4l2src",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "outward_nrt_width": {
            "type": "int",
            "value": 1920,
            "desc": "width for outward camera nrt pipeline",
            "rules": {
                "allowed_values": [
                    1920,
                    3840
                ]
            }
        },
        "outward_nrt_height": {
            "type": "int",
            "value": 1080,
            "desc": "height for outward camera nrt pipeline",
            "rules": {
                "allowed_values": [
                    1080,
                    2160
                ]
            }
        },
        "outward_nrt_fps": {
            "type": "int",
            "value": 30,
            "desc": "fps for outward camera nrt pipeline",
            "rules": {
                "min": 15,
                "max": 60
            }
        },
        "outward_nrt_bitrate": {
            "type": "int",
            "value": 6000000,
            "desc": "bitrate for outward camera nrt pipeline",
            "rules": {
                "min": 200000,
                "max": 24000000
            }
        },
        "outward_ld_enabled": {
            "type": "bool",
            "value": true,
            "desc": "outward low def enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "outward_nrt_ld_width": {
            "type": "int",
            "value": 854,
            "desc": "width for outward camera nrt ld pipeline",
            "rules": {
            "allowed_values": [
                854,
                1280,
                1920
            ]
            }
        },
        "outward_nrt_ld_height": {
            "type": "int",
            "value": 480,
            "desc": "height for outward camera nrt ld pipeline",
            "rules": {
            "allowed_values": [
                480,
                720,
                1080
            ]
            }
        },
        "outward_nrt_ld_bitrate": {
            "type": "int",
            "value": 1000000,
            "desc": "bitrate for outward camera nrt ld pipeline",
            "rules": {
            "min": 200000,
            "max": 24000000
            }
        },
        "inward_nrt_width": {
            "type": "int",
            "value": 1920,
            "desc": "width for inward camera nrt pipeline",
            "rules": {
                "allowed_values": [
                    1280,
                    1920,
                    3840
                ]
            }
        },
        "inward_nrt_height": {
            "type": "int",
            "value": 1080,
            "desc": "height for inward camera nrt pipeline",
            "rules": {
                "allowed_values": [
                    720,
                    1080,
                    2160
                ]
            }
        },
        "inward_nrt_fps": {
            "type": "int",
            "value": 15,
            "desc": "fps for inward camera nrt pipeline",
            "rules": {
                "min": 15,
                "max": 60
            }
        },
        "inward_nrt_bitrate": {
            "type": "int",
            "value": 2000000,
            "desc": "bitrate for inward camera nrt pipeline",
            "rules": {
                "min": 200000,
                "max": 24000000
            }
        },
        "inward_ld_enabled": {
            "type": "bool",
            "value": true,
            "desc": "inward low def enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "inward_nrt_ld_width": {
            "type": "int",
            "value": 854,
            "desc": "width for inward camera nrt ld pipeline",
            "rules": {
            "allowed_values": [
                854,
                1280,
                1920
            ]
            }
        },
        "inward_nrt_ld_height": {
            "type": "int",
            "value": 480,
            "desc": "height for inward camera nrt ld pipeline",
            "rules": {
            "allowed_values": [
                480,
                720,
                1080
            ]
            }
        },
        "inward_nrt_ld_bitrate": {
            "type": "int",
            "value": 500000,
            "desc": "bitrate for inward camera nrt ld pipeline",
            "rules": {
            "min": 200000,
            "max": 24000000
            }
        },
        "leftcam_nrt_bitrate": {
            "type": "int",
            "value": 500000,
            "desc": "bitrate for left camera nrt pipeline",
            "rules": {
                "min": 200000,
                "max": 24000000
            }
        },
        "rightcam_nrt_bitrate": {
            "type": "int",
            "value": 500000,
            "desc": "bitrate for right camera nrt pipeline",
            "rules": {
                "min": 200000,
                "max": 24000000
            }
        },
        "interpolation_method": {
            "type": "int",
            "value": 3,
            "desc": "image resizing interpolation method, default is 10-tap. (0: Nearest, 1: Bilinear, 2: 5-Tap, 3: 10-Tap, 4: Smart, 5: Nicest)",
            "rules": {
                "min": 0,
                "max": 5
            }
        }
    },
    "data_products": {
        "enabled": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable or disable low fps implementation for data products",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "bitrate": {
            "type": "int",
            "value": 1000000,
            "desc": "bitrate of the encoded  data",
            "rules": {
                "min": 200000,
                "max": 6000000
            }
        },
        "fps": {
            "type": "int",
            "value": 5,
            "desc": "framerate of the encoded data",
            "rules": {
                "min": 1,
                "max": 10
            }
        },
        "iframeinterval": {
            "type": "int",
            "value": 5,
            "desc": "I frame interval of the encoded data",
            "rules": {
                "min": 1,
                "max": 10
            }
        }
    },
    "ea_config": {
        "enabled": {
            "type": "int",
            "value": 0,
            "desc": "Configuration to enable/disable the Event Access Preview Feature",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "outward": {
            "type": "int",
            "value": 0,
            "desc": "Configuration to enable outward camera image capture",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "inward": {
            "type": "int",
            "value": 0,
            "desc": "Configuration to enable inward camera image capture",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "width": {
            "type": "int",
            "value": 206,
            "desc": "Width of the image",
            "rules": {
                "allowed_values": [
                    206
                ]
            }
        },
        "height": {
            "type": "int",
            "value": 112,
            "desc": "Height of the image",
            "rules": {
                "allowed_values": [
                    112
                ]
            }
        },
        "quality": {
            "type": "int",
            "value": 70,
            "desc": "quality factor of the images",
            "rules": {
                "min": 1,
                "max": 100
            }
        },
        "num_images_per_hour": {
            "type": "int",
            "value": 12,
            "desc": "Frequency of images captured per hour",
            "rules": {
                "allowed_values": [
                    60,
                    30,
                    20,
                    12
                ]
            }
        },
        "ea_batch_frequency": {
            "type": "int",
            "value": 10,
            "desc": "Time difference in minutes between two batch upload calls.",
            "rules": {
                "allowed_values": [
                    3,
                    5,
                    10
                ]
            }
        }
    },
    "imu": {
        "gyro_scale": {
            "type": "int",
            "value": 1,
            "desc": "sets the gyro scale for imu data",
            "rules": {
                "allowed_values": [
                    0,
                    1,
                    2,
                    3
                ]
            }
        },
        "gyro_dlpf_bw": {
            "type": "int",
            "value": 5,
            "desc": "sets the Gyroscope Digital Low-Pass Filter Bandwidth for imu data",
            "rules": {
                "allowed_values": [
                    -1,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7
                ]
            }
        },
        "accel_scale": {
            "type": "int",
            "value": 1,
            "desc": "sets accelation scale ",
            "rules": {
                "allowed_values": [
                    0,
                    1,
                    2,
                    3
                ]
            }
        },
        "accel_dlpf_bw": {
            "type": "int",
            "value": 6,
            "desc": "sets the acceleration Digital Low-Pass Filter Bandwidth for imu data",
            "rules": {
                "allowed_values": [
                    -1,
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7
                ]
            }
        },
        "sample_rate": {
            "type": "int",
            "value": 20,
            "desc": "number of imu data samples per second",
            "rules": {
                "allowed_values": [
                    10,
                    20,
                    50,
                    100,
                    200,
                    500
                ]
            }
        },
        "invert_accel_x": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for acceleration x axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "invert_accel_y": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for acceleration y axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "invert_accel_z": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for acceleration z axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "invert_gyro_x": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for gyro x axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "invert_gyro_y": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for gyro y axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "invert_gyro_z": {
            "type": "string",
            "value": "true",
            "desc": "inversion configuration flag for gyro z axis orientation",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        }
    },
    "power": {
        "cyclic_reboot_duration": {
            "type": "int",
            "value": 960,
            "desc": "cyclic reboot duration in mins; we cant disable this",
            "rules": {
                "min": 20,
                "max": 2147483647
            }
        },
        "crank_shutdown_duration": {
            "type": "int",
            "value": 15,
            "desc": "time in mins to keep the device on after crank voltage goes down",
            "rules": {
                "min": 1,
                "max": 2147483647
            }
        },
        "battery_health_monitor": {
            "type": "string",
            "value": "on",
            "desc": "consider battery voltage to shutdown",
            "rules": {
                "allowed_values": [
                    "on",
                    "off"
                ],
                "max_length": 3
            }
        },
        "enable_lowpowermode": {
            "type": "string",
            "value": "on",
            "desc": "wake during engine idle",
            "rules": {
                "allowed_values": [
                    "on",
                    "off"
                ],
                "max_length": 3
            }
        },
        "lpw_no_record": {
            "type": "bool",
            "value": false,
            "desc": "true -> No recording in LPW, false -> recording in LPW",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "max_lowpower_wakeups": {
            "type": "int",
            "value": 56,
            "desc": "max LPW wakeups",
            "rules": {
                "min": 1,
                "max": 2147483647
            }
        },
        "lowpower_wakeup_cycle_duration": {
            "type": "int",
            "value": 180,
            "desc": "LPW wakeup cycle mins",
            "rules": {
                "min": 30,
                "max": 2147483647
            }
        },
        "lowpower_wakeup_duration": {
            "type": "int",
            "value": 6,
            "desc": "LPW wake duration mins",
            "rules": {
                "min": 2,
                "max": 960
            }
        },
        "lowpower_wakeup_long_cycle_threshold": {
            "type": "int",
            "value": 32,
            "desc": "long cycle threshold",
            "rules": {
                "min": 2,
                "max": 2147483647
            }
        },
        "lowpower_wakeup_long_cycle_duration": {
            "type": "int",
            "value": 1440,
            "desc": "long cycle duration mins",
            "rules": {
                "min": 30,
                "max": 2147483647
            }
        },
        "min_voltage_limit_V": {
            "type": "float",
            "value": 10.01,
            "desc": "0.0 to 20.1 (increments of 0.01 volts)",
            "rules": {
                "min": 0.0,
                "max": 20.1
            }
        },
        "max_voltage_limit_V": {
            "type": "float",
            "value": 15.01,
            "desc": "0.0 to 20.1 (increments of 0.01 volts)",
            "rules": {
                "min": 0.0,
                "max": 20.1
            }
        },
        "min_voltage_limit_24V": {
            "type": "float",
            "value": 20.01,
            "desc": "0.0 to 35.1 (increments of 0.01 volts)",
            "rules": {
                "min": 0.0,
                "max": 35.1
            }
        },
        "max_voltage_limit_24V": {
            "type": "float",
            "value": 30.01,
            "desc": "0.0 to 35.1 (increments of 0.01 volts)",
            "rules": {
                "min": 0,
                "max": 35.1
            }
        },
        "abnormal_voltage_wait_duration": {
            "type": "int",
            "value": 3,
            "desc": "used to indicate threshold for low battery volt count",
            "rules": {
                "min": 1,
                "max": 2880
            }
        },
        "allow_sdcard_reboot_freq": {
            "type": "int",
            "value": 86400,
            "desc": "sdcard RO reboot period secs",
            "rules": {
                "min": 3600,
                "max": 2147483647
            }
        },
        "fsck_lowpower_wakeup": {
            "type": "int",
            "value": 2,
            "desc": "LPW wakeup number fsck",
            "rules": {
                "min": 1,
                "max": 2147483647
            }
        },
        "suspend_mode": {
            "type": "string",
            "value": "on",
            "desc": "SC7 suspend mode",
            "rules": {
                "allowed_values": [
                    "on",
                    "off"
                ],
                "max_length": 3
            }
        },
        "max_ignition": {
            "type": "int",
            "value": 10,
            "desc": "max ignition per session for metadata",
            "rules": {
                "min": 1,
                "max": 60
            }
        },
        "ignition_on_audio_alert": {
            "type": "bool",
            "value": false,
            "desc": "audio on ignition ON",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ignition_on_audio_alert_interval": {
            "type": "int",
            "value": 30,
            "desc": "interval between ignition alerts secs",
            "rules": {
                "min": 30,
                "max": 600
            }
        },
        "ignition_on_audio_alert_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/Seatbelt_alert.wav",
            "desc": "ignition audio file",
            "rules": {
                "max_length": 256
            }
        },
        "ignition_on_idle_audio_alert": {
            "type": "bool",
            "value": false,
            "desc": "audio on ignition ON & idling",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ignition_on_idle_audio_alert_duration": {
            "type": "int",
            "value": 5,
            "desc": "idle audio alert duration mins",
            "rules": {
                "min": 1,
                "max": 10
            }
        },
        "ignition_on_idle_audio_alert_frequency": {
            "type": "int",
            "value": 0,
            "desc": "max idle audio alerts (0 unlimited)",
            "rules": {
                "min": 0,
                "max": 2147483647
            }
        },
        "ignition_on_idle_audio_alert_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/Seatbelt_alert.wav",
            "desc": "idle audio file",
            "rules": {
                "max_length": 256
            }
        },
        "frequent_low_power_wakeup": {
            "type": "string",
            "value": "disable",
            "desc": "frequent LPW schedule",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "extended_post_ignition_off_and_lpw_timer": {
            "type": "bool",
            "value": false,
            "desc": "delay shutdown if vods pending",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "delay_reboot_time": {
            "type": "int",
            "value": 900,
            "desc": "delay threshold reboot secs",
            "rules": {
                "min": 60,
                "max": 2147483647
            }
        },
        "max_B2B_reboot_allowed": {
            "type": "int",
            "value": 10,
            "desc": "max back to back reboots",
            "rules": {
                "min": 2,
                "max": 50
            }
        },
        "non_lpm_wakeup_duration": {
            "type": "int",
            "value": 6,
            "desc": "non LPM wake duration mins",
            "rules": {
                "min": 1,
                "max": 1440
            }
        },
        "nlpm_on_imu": {
            "type": "bool",
            "value": false,
            "desc": "wake WOM disables LPW",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "nlpm_on_por": {
            "type": "bool",
            "value": false,
            "desc": "wake AON disables LPW",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "nlpm_on_ign": {
            "type": "bool",
            "value": false,
            "desc": "wake ignition toggle disables LPW",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "nlpm_on_misc": {
            "type": "bool",
            "value": false,
            "desc": "wake misc disables LPW",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "misc_wakeup_duration": {
            "type": "int",
            "value": 3,
            "desc": "wakeup duration in minutes for non RTC wakeup reasons",
            "rules": {
                "min": 1,
                "max": 960
            }
        },
        "cpusched_enable": {
            "type": "bool",
            "value": false,
            "desc": "To enable different cpu frequency setting based on different triggers like supercap, normal, low_power_mode",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "home_ssid": {
        "wifi_fallback": {
            "type": "string",
            "value": "disable",
            "desc": "home ssid wifi fallback",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        }
    },
    "wifi": {
        "wifi_fallback": {
            "type": "string",
            "value": "enable",
            "desc": "wifi fallback",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "count": {
            "type": "int",
            "value": 1,
            "desc": "wifi networks count",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "wifiname0": {
            "type": "string",
            "value": "Netradyne-SSID",
            "desc": "ssid name",
            "rules": {
                "max_length": 5000
            }
        },
        "wifipassword0": {
            "type": "string",
            "value": "12345678",
            "desc": "ssid password",
            "rules": {
                "max_length": 5000
            }
        },
        "ping_response_threshold_msecs": {
            "type": "int",
            "value": 5000,
            "desc": "threshold for ping response",
            "rules": {
                "min": 0,
                "max": 10000
            }
        },
        "ssid_availability_check_interval_secs": {
            "type": "int",
            "value": 30,
            "desc": "interval for checking availability of SSIDs",
            "rules": {
                "min": 10,
                "max": 60
            }
        },
        "wifi_connection_test_interval_secs": {
            "type": "int",
            "value": 60,
            "desc": "interval for monitoring connection status",
            "rules": {
                "min": 10,
                "max": 120
            }
        },
        "wifi_connect_task_timeout_secs": {
            "type": "int",
            "value": 60,
            "desc": "timed task timeout for wifi connection",
            "rules": {
                "min": 30,
                "max": 120
            }
        },
        "wifi_reconnect_interval_msecs": {
            "type": "int",
            "value": 180000,
            "desc": "Gap between a wifi disconnect and retrying connection",
            "rules": {
                "allowed_values": [
                    180000
                ]
            }
        }
    },
    "svc": {
        "diskmon_enable": {
            "type": "bool",
            "value": true,
            "desc": "disk monitor enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ],
                "max_length": 5
            }
        },
        "diskmon_poll": {
            "type": "int",
            "value": 180,
            "desc": "disk monitor poll secs",
            "rules": {
                "min": 1,
                "max": 86400
            }
        },
        "config_recovery_enable": {
            "type": "bool",
            "value": true,
            "desc": "config recovery enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ],
                "max_length": 5
            }
        },
        "config_recovery_poll": {
            "type": "int",
            "value": 900,
            "desc": "config recovery poll secs",
            "rules": {
                "min": 1,
                "max": 86400
            }
        }
    },
    "speaker_v2": {
        "volume": {
            "type": "int",
            "value": 5,
            "desc": "volume represents level here, 1 to 5, default is 3 (105 old speaker, 11500 new speaker)",
            "rules": {
                "min": 1,
                "max": 5
            }
        }
    },
    "speaker": {
        "volume": {
            "type": "int",
            "value": 11,
            "desc": "this value will be used to calculate the effective volume (default is 11500)",
            "rules": {
                "min": 1,
                "max": 100
            }
        },
        "fraction": {
            "type": "int",
            "value": 25,
            "desc": "speaker fraction can take value from 0 to 99;",
            "rules": {
                "min": 0,
                "max": 99
            }
        }
    },
    "GPU_frequency_setting": {
        "frequency_khz": {
            "type": "int",
            "value": 998400,
            "desc": "Not being used",
            "rules": {
                "allowed_values": [
                    76800,
                    153600,
                    230400,
                    307200,
                    384000,
                    460800,
                    537600,
                    614400,
                    691200,
                    768000,
                    844800,
                    921600,
                    998400
                ]
            }
        }
    },
    "streaming": {
        "enable_streaming": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable RT streaming",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "outwardcam_streaming": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable outward RT streaming",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "inwardcam_streaming": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable inward RT streaming",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "inertial_streaming": {
            "type": "string",
            "value": "true",
            "desc": "inertial streaming enabled",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "post_ignition_analytics_secs": {
            "type": "int",
            "value": 0,
            "desc": "send frames to analytics these many seconds post ignition",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "post_ignition_analytics_secs_outward": {
            "type": "int",
            "value": 0,
            "desc": "outward post ignition analytics secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        }
    },
    "outwardcam_streaming": {
        "analytics_fps": {
            "type": "int",
            "value": 5,
            "desc": "analytics fps outward",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "width": {
            "type": "int",
            "value": 640,
            "desc": "outward analytics width",
            "rules": {
                "min": 640,
                "max": 1920
            }
        },
        "height": {
            "type": "int",
            "value": 360,
            "desc": "outward analytics height",
            "rules": {
                "min": 360,
                "max": 1080
            }
        },
        "enable_crop": {
            "type": "string",
            "value": "false",
            "desc": "(Not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "crop_top_left_x": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 3840
            }
        },
        "crop_top_left_y": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 2160
            }
        },
        "crop_top_right_x": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 3840
            }
        },
        "crop_top_right_y": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 2160
            }
        },
        "crop_bottom_left_x": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 3840
            }
        },
        "crop_bottom_left_y": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 2160
            }
        },
        "crop_bottom_right_x": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 3840
            }
        },
        "crop_bottom_right_y": {
            "type": "int",
            "value": -1,
            "desc": "(Not using)",
            "rules": {
                "min": -1,
                "max": 2160
            }
        }
    },
    "inwardcam_streaming": {
        "analytics_fps": {
            "type": "int",
            "value": 5,
            "desc": "analytics fps inward",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "width": {
            "type": "int",
            "value": 640,
            "desc": "inward analytics width",
            "rules": {
                "min": 640,
                "max": 1920
            }
        },
        "height": {
            "type": "int",
            "value": 360,
            "desc": "inward analytics height",
            "rules": {
                "min": 360,
                "max": 1080
            }
        },
        "enable_crop": {
            "type": "string",
            "value": "false",
            "desc": "(Not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        }
    },
    "INSTALLER_APP": {
        "static_ip": {
            "type": "string",
            "value": "10.42.0.1",
            "desc": "static ip",
            "rules": {
                "max_length": 5000
            }
        },
        "static_port": {
            "type": "int",
            "value": 5000,
            "desc": "static port",
            "rules": {
                "min": 1,
                "max": 65535
            }
        },
        "socket_timeout": {
            "type": "int",
            "value": 90,
            "desc": "socket timeout secs",
            "rules": {
                "min": 1,
                "max": 3600
            }
        },
        "generic_cmd_feature": {
            "type": "bool",
            "value": true,
            "desc": "generic cmd feature",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "inst_long_press_duration_ms": {
            "type": "int",
            "value": 10000,
            "desc": "installer long press ms",
            "rules": {
                "min": 0,
                "max": 600000
            }
        },
        "keep_alive_timeout_s": {
            "type": "int",
            "value": 45,
            "desc": "keep alive timeout secs",
            "rules": {
                "min": 1,
                "max": 3600
            }
        },
        "ota_advertise_duration_s": {
            "type": "int",
            "value": 7,
            "desc": "OTA advertise duration secs",
            "rules": {
                "min": 1,
                "max": 3600
            }
        },
        "installer_mode_max_time": {
            "type": "int",
            "value": 15,
            "desc": "max installer mode mins",
            "rules": {
                "min": 1,
                "max": 1440
            }
        },
        "installer_led_blink_timeout_sec": {
            "type": "int",
            "value": 240,
            "desc": "Time duration for which LED should blink to indicate installer app connection in seconds",
            "rules": {
                "min": 1,
                "max": 3600
            }
        }
    },
    "process_mon": {
        "error_report": {
            "type": "bool",
            "value": true,
            "desc": "error report enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "can_alerts": {
        "enabled": {
            "type": "int",
            "value": 1,
            "desc": "0/1 1 - enabled 0 - disabled",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "socket": {
            "type": "string",
            "value": "ipc:///dev/shm/MSGQ/6359",
            "desc": "zmq socket",
            "rules": {
                "max_length": 5000
            }
        },
        "speed": {
            "type": "int",
            "value": 1000,
            "desc": "wheel based vehicle speed freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "rpm": {
            "type": "int",
            "value": 1000,
            "desc": "Engine speed freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "odo": {
            "type": "int",
            "value": 0,
            "desc": "Odometer freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "e_hrs": {
            "type": "int",
            "value": 0,
            "desc": "Engine hours freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "fcw": {
            "type": "int",
            "value": 1000,
            "desc": "Forward Collision Warning freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "rop": {
            "type": "int",
            "value": 1000,
            "desc": "Roll Over Protectin Stability freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "br_sw": {
            "type": "int",
            "value": 1000,
            "desc": "Brake switch freq",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "br_pad_pos": {
            "type": "int",
            "value": 0,
            "desc": "Brake Pad Position ",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "fcw_dur": {
            "type": "int",
            "value": 1,
            "desc": "Forward Collision Warning Duration",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "turn_signal": {
            "type": "int",
            "value": 1000,
            "desc": "Turn Signal",
            "rules": {
                "min": 0,
                "max": 60000
            }
        },
        "seat_belt_sw": {
            "type": "int",
            "value": 1000,
            "desc": "Seat Belt",
            "rules": {
                "min": 0,
                "max": 60000
            }
        }
    },
    "iosix": {
        "num_cmds": {
            "type": "int",
            "value": 13,
            "desc": "number of comands to be sent to vbus during initial configuration",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "cmd_1": {
            "type": "string",
            "value": "ENABLE,9",
            "desc": "vbus command(can we add the check for non alphanumeric)",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_2": {
            "type": "string",
            "value": "ENABLE,0",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_3": {
            "type": "string",
            "value": "ENABLE,1",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_4": {
            "type": "string",
            "value": "ENABLE,3",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_5": {
            "type": "string",
            "value": "ENABLE,4",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_6": {
            "type": "string",
            "value": "ENABLE,5",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_7": {
            "type": "string",
            "value": "ENABLE,6",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_8": {
            "type": "string",
            "value": "ENABLE,7",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_9": {
            "type": "string",
            "value": "ENABLE,8",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_10": {
            "type": "string",
            "value": "ENABLE,10",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_11": {
            "type": "string",
            "value": "ENABLE,11",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_12": {
            "type": "string",
            "value": "ENABLE,12",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "cmd_13": {
            "type": "string",
            "value": "INTERVAL,10000",
            "desc": "vbus command",
            "rules": {
                "max_length": 5000
            }
        },
        "set_time": {
            "type": "bool",
            "value": true,
            "desc": "Sends epoch time from Driveri to VBUS after every connection when true",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "obd_prop": {
            "type": "bool",
            "value": true,
            "desc": "enable obd property in VBUS, sends an ENABLE,8 when true",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "hostip": {
            "type": "string",
            "value": "192.168.4.1",
            "desc": "host ip address to connect to VBUS",
            "rules": {
                "max_length": 5000
            }
        },
        "port": {
            "type": "int",
            "value": 23,
            "desc": "port number to connect to VBUS",
            "rules": {
                "min": 1,
                "max": 65535
            }
        },
        "ble_reset_on_start": {
            "type": "string",
            "value": "false",
            "desc": "On Driveri device bootup send a BT reset to VBUS",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "client_mode": {
            "type": "string",
            "value": "true",
            "desc": "enable VBUS to connect to Driveri AP",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "send_wifi_credentials": {
            "type": "string",
            "value": "false",
            "desc": "send wifi credentials to VBUS when in client mode",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "vbus_request_max_threshold": {
            "type": "int",
            "value": 1,
            "desc": "Total number of records to be requested from VBUS",
            "rules": {
                "min": 1,
                "max": 100
            }
        },
        "vbus_reset_timeout": {
            "type": "int",
            "value": 30,
            "desc": "VBUS reset timeout in mins, sends a bt reset when connection errors found.",
            "rules": {
                "min": 30,
                "max": 1440
            }
        },
        "vbus_send_wifi_cred_timeout": {
            "type": "int",
            "value": 2,
            "desc": "VBUS send wifi credentials timeout in mins, sends wifi credentials when connection errors found.",
            "rules": {
                "min": 2,
                "max": 1440
            }
        },
        "gps_treshold_time_engineoff": {
            "type": "int",
            "value": 600,
            "desc": "timeout in secs to send BT reset if engine off but GPS speed > 5mph",
            "rules": {
                "min": 600,
                "max": 86400
            }
        },
        "engine_debounce_count": {
            "type": "int",
            "value": 5,
            "desc": "if obd service send unknow status for 5 time and shutdown time over then device will switch off",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "flash_vbus_fw": {
            "type": "bool",
            "value": true,
            "desc": "enabled/disables vbus fw flash from driveri",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "power_comp_check_dur": {
            "type": "int",
            "value": 2,
            "desc": "duration in hours to check vbus power compliances",
            "rules": {
                "min": 1,
                "max": 24
            }
        },
        "load_obd_prop": {
            "type": "bool",
            "value": true,
            "desc": "Config for enabling proprietary OBD pids for VBUS V3",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "socket_issue_reset_time": {
            "type": "int",
            "value": 30,
            "desc": "time in mins to reset VBUS if socket issue is there",
            "rules": {
                "min": 30,
                "max": 1440
            }
        }
    },
    "obd_encryption": {
        "seed": {
            "type": "string",
            "value": "1be227a28b5eff3c9102",
            "desc": "seed secret",
            "rules": {
                "max_length": 5000
            }
        },
        "encryption_state": {
            "type": "int",
            "value": 0,
            "desc": "default encryption status (1 - encrypted, 0 - not encrypted)",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "interval": {
            "type": "int",
            "value": 60,
            "desc": "time interval boundary in seconds for encryption",
            "rules": {
                "min": 0,
                "max": 86400,
                "allowed_values": [
                    60
                ]
            }
        },
        "min_firmware_version": {
            "type": "string",
            "value": "6H",
            "desc": "minimum firmware version that supports encryption",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "can_lib_param": {
        "canlib_dur": {
            "type": "int",
            "value": 24,
            "desc": "duration in hours to send can lib params",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "num_param": {
            "type": "int",
            "value": 12,
            "desc": "num of can lib params",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "param_1": {
            "type": "string",
            "value": "Fuel",
            "desc": "Fuel param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_2": {
            "type": "string",
            "value": "Diagnostic",
            "desc": "Diagnostic param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_3": {
            "type": "string",
            "value": "States",
            "desc": "States param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_4": {
            "type": "string",
            "value": "Engine",
            "desc": "Engine param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_5": {
            "type": "string",
            "value": "Driver",
            "desc": "Driver param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_6": {
            "type": "string",
            "value": "Assist",
            "desc": "Assist param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_7": {
            "type": "string",
            "value": "Transmission",
            "desc": "Transmission param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_8": {
            "type": "string",
            "value": "Emissions",
            "desc": "Emissions param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_9": {
            "type": "string",
            "value": "Electric",
            "desc": "Electric param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_10": {
            "type": "string",
            "value": "Tires",
            "desc": "Tires param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_11": {
            "type": "string",
            "value": "Data",
            "desc": "Data param",
            "rules": {
                "max_length": 5000
            }
        },
        "param_12": {
            "type": "string",
            "value": "Values",
            "desc": "Values param",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "vehicle_data": {
        "enabled": {
            "type": "bool",
            "value": false,
            "desc": "vehicle data enablement status ",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "unpair_vbus": {
            "type": "string",
            "value": "abcd,1234",
            "desc": "Comma-separated of VBUS ID and epoch",
            "rules": {
                "max_length": 5000
            }
        },
        "iosix_enabled": {
            "type": "bool",
            "value": true,
            "desc": "tells whether iosix is enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "obd2_enabled": {
            "type": "bool",
            "value": false,
            "desc": "tells whether obd2 is enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "j1939_enabled": {
            "type": "bool",
            "value": true,
            "desc": "tells whethers j1939 is enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "extra_logging": {
            "type": "bool",
            "value": false,
            "desc": "tells whether extra logging is enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "retry_count": {
            "type": "int",
            "value": 300,
            "desc": "retry count for ndmb client of obd",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "retry_time": {
            "type": "int",
            "value": 10,
            "desc": "retry time for ndmb client of obd",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "disable_level": {
            "type": "int",
            "value": 2,
            "desc": "1 warning 2 error 3 bussoff, bussoff shall not use the count",
            "rules": {
                "allowed_values": [
                    1,
                    2
                ]
            }
        },
        "disable_count": {
            "type": "int",
            "value": 10,
            "desc": "wait count before creating CAN error state file ",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "engine_off_rpm": {
            "type": "int",
            "value": 300,
            "desc": "engine off rpm",
            "rules": {
                "min": 50,
                "max": 8000
            }
        },
        "engine_on_voltage_12V": {
            "type": "float",
            "value": 13.1,
            "desc": "In 12V, battery voltage level after which its considered engine is on",
            "rules": {
                "min": 0,
                "max": 20
            }
        },
        "engine_on_voltage_24V": {
            "type": "float",
            "value": 26.5,
            "desc": "In 24V, battery voltage level after which its considered engine is on",
            "rules": {
                "min": 0,
                "max": 40
            }
        },
        "can_bus_bw_limit": {
            "type": "int",
            "value": 60,
            "desc": " Bus load in percentage after which CAN will be disabled",
            "rules": {
                "min": 30,
                "max": 99
            }
        },
        "vin_and_pd_only": {
            "type": "bool",
            "value": false,
            "desc": "Amazon requirement, if true, just vin in broadcast and ADC",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "j1939_request_spn": {
            "type": "bool",
            "value": true,
            "desc": "Default request based spn 247, 250 are enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "vinvox_request_interval": {
            "type": "int",
            "value": 10,
            "desc": "interval in mins to send vinvox request",
            "rules": {
                "min": 0,
                "max": 1440
            }
        }
    },
    "driveri_one": {
        "mqtt_qos_level": {
            "type": "int",
            "value": 1,
            "desc": "MQTT Quality of Service level ",
            "rules": {
                "min": 0,
                "max": 2
            }
        },
        "fuel_report": {
            "type": "int",
            "value": 0,
            "desc": "enables/disables fuel report",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "fuel_report_interval": {
            "type": "int",
            "value": 1800,
            "desc": "interval in seconds to send the fuel report to cloud",
            "rules": {
                "min": 10,
                "max": 86400
            }
        },
        "idling_report": {
            "type": "int",
            "value": 0,
            "desc": "enables/disables idling report",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "idling_threshold_time": {
            "type": "int",
            "value": 30,
            "desc": "interval in seconds for which rpm > 0 & speed = 0 to detect engine idling",
            "rules": {
                "min": 1,
                "max": 3600
            }
        },
        "idling_threshold_entry_speed": {
            "type": "int",
            "value": 0,
            "desc": "speed in mph after which start of idling is considered",
            "rules": {
                "min": 0,
                "max": 250
            }
        },
        "idling_threshold_exit_speed": {
            "type": "int",
            "value": 5,
            "desc": "speed in mph after which end of idling is considered",
            "rules": {
                "min": 0,
                "max": 250
            }
        },
        "eld_enabled": {
            "type": "bool",
            "value": false,
            "desc": "enables/disables eld",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "gps_tracking_enabled": {
            "type": "bool",
            "value": false,
            "desc": "enables/disables gps tracking",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "offline_storage": {
            "type": "int",
            "value": 240,
            "desc": "number of hours of data to be stored on disk",
            "rules": {
                "min": 1,
                "max": 480
            }
        },
        "speed": {
            "type": "int",
            "value": 10,
            "desc": "frequency in seconds at which speed is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "rpm": {
            "type": "int",
            "value": 40,
            "desc": "frequency in seconds at which rpm is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "e_hrs": {
            "type": "int",
            "value": 40,
            "desc": "frequency in seconds at which engine hours is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "odo": {
            "type": "int",
            "value": 40,
            "desc": "frequency in seconds at which odometer reading is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "vin": {
            "type": "int",
            "value": 40,
            "desc": "frequency in seconds at which VIN is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "loc": {
            "type": "int",
            "value": 40,
            "desc": "frequency in seconds at which the gps location is published",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "loc_samples": {
            "type": "int",
            "value": 1,
            "desc": "number of samples of gps location to be published, must be less than or equal to loc, maximum is 60",
            "rules": {
                "min": 0,
                "max": 60
            }
        },
        "odo_dist": {
            "type": "int",
            "value": 8,
            "desc": "distance in kms after which update will be trasmitted, if not sent within last interval.",
            "rules": {
                "min": 1,
                "max": 86400
            }
        },
        "dtc_report": {
            "type": "int",
            "value": 0,
            "desc": "enables dtc report feature if value is \"1\"",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "vehicle_health": {
            "type": "bool",
            "value": false,
            "desc": "enables/disables vehicle health",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "bucket_0_recording_time": {
            "type": "int",
            "value": 0,
            "desc": "this will not be configurable",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "bucket_1_recording_time": {
            "type": "int",
            "value": 10,
            "desc": "publish interval for bucket1",
            "rules": {
                "min": 1,
                "max": 86400
            }
        },
        "bucket_2_recording_time": {
            "type": "int",
            "value": 30,
            "desc": "publish interval for bucket2",
            "rules": {
                "min": 1,
                "max": 86400
            }
        },
        "bucket_3_recording_time": {
            "type": "int",
            "value": 60,
            "desc": "publish interval for bucket3",
            "rules": {
                "min": 1,
                "max": 86400
            }
        },
        "auto_fw_download": {
            "type": "bool",
            "value": false,
            "desc": "Enable or disable firmware flash feature",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ],
                "max_length": 5
            }
        }
    },
    "Transcode": {
        "enable": {
            "type": "bool",
            "value": true,
            "desc": "true, 1 or false, 0. decides both inward and outward transcoding",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "num_hq_videos_max": {
            "type": "int",
            "value": 120,
            "desc": "number of videos to store in HQ",
            "rules": {
                "min": 1,
                "max": 6000
            }
        },
        "delay": {
            "type": "int",
            "value": 10,
            "desc": "delay in seconds before the next transcode is taken up",
            "rules": {
                "min": 10,
                "max": 3600
            }
        },
        "enable_smart_transcode": {
            "type": "bool",
            "value": true,
            "desc": "if enabled, uses values from analytics to determine compression type",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "enable_inward_transcode": {
            "type": "bool",
            "value": true,
            "desc": "if enabled, transcodes inward videos. if enable = false, this value is ignored",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "time_sync": {
        "enabled": {
            "type": "bool",
            "value": true,
            "desc": "time sync enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "gps_time_sync_enable": {
            "type": "bool",
            "value": true,
            "desc": "gps time sync",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "network_time_sync_enable": {
            "type": "bool",
            "value": true,
            "desc": "network time sync",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "device_store": {
        "store_path": {
            "type": "string",
            "value": "\"/home/ubuntu/.nddevice/latest/store.nds\"",
            "desc": "device store path",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "J1939_params": {
        "num_params": {
            "type": "int",
            "value": 33,
            "desc": "num of spn configured (max can be 22 + VIN)",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "param_1": {
            "type": "string",
            "value": "0x54",
            "desc": "speed",
            "rules": {
                "max_length": 5000
            }
        },
        "param_1_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for speed",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_2": {
            "type": "string",
            "value": "0xBE",
            "desc": "rpm",
            "rules": {
                "max_length": 5000
            }
        },
        "param_2_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for rpm",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_3": {
            "type": "string",
            "value": "0x33",
            "desc": "throttle position",
            "rules": {
                "max_length": 5000
            }
        },
        "param_3_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for throttle position",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_4": {
            "type": "string",
            "value": "0x93F",
            "desc": "left turn signal",
            "rules": {
                "max_length": 5000
            }
        },
        "param_4_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for left turn signal",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_5": {
            "type": "string",
            "value": "0x941",
            "desc": "right turn signal",
            "rules": {
                "max_length": 5000
            }
        },
        "param_5_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for right turn signal",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_6": {
            "type": "string",
            "value": "0x633",
            "desc": "distance to forward vehicle",
            "rules": {
                "max_length": 5000
            }
        },
        "param_6_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for distance to forward vehicle",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_7": {
            "type": "string",
            "value": "0x636",
            "desc": "adaptive cruise control mode",
            "rules": {
                "max_length": 5000
            }
        },
        "param_7_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for adaptive cruise control mode",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_8": {
            "type": "string",
            "value": "0x233",
            "desc": "anti lock braking active",
            "rules": {
                "max_length": 5000
            }
        },
        "param_8_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for anti lock braking active",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_9": {
            "type": "string",
            "value": "0xF5",
            "desc": "total vehicle distance",
            "rules": {
                "max_length": 5000
            }
        },
        "param_9_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for total vehicle distance",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_10": {
            "type": "string",
            "value": "0x255",
            "desc": "brake switch",
            "rules": {
                "max_length": 5000
            }
        },
        "param_10_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for brake switch",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_11": {
            "type": "string",
            "value": "0x5B",
            "desc": "accelerator pedal position",
            "rules": {
                "max_length": 5000
            }
        },
        "param_11_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for accelerator pedal position",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_12": {
            "type": "string",
            "value": "0xFA",
            "desc": "fuel used",
            "rules": {
                "max_length": 5000
            }
        },
        "param_12_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for fuel used",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_13": {
            "type": "string",
            "value": "0xF7",
            "desc": "engine hours",
            "rules": {
                "max_length": 5000
            }
        },
        "param_13_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for engine hours",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_14": {
            "type": "string",
            "value": "0x209",
            "desc": "brake pedal position",
            "rules": {
                "max_length": 5000
            }
        },
        "param_14_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for brake pedal position",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_15": {
            "type": "string",
            "value": "0x20F",
            "desc": "cruise control status",
            "rules": {
                "max_length": 5000
            }
        },
        "param_15_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for cruise control status",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_16": {
            "type": "string",
            "value": "0x395",
            "desc": "High precision odometer",
            "rules": {
                "max_length": 5000
            }
        },
        "param_16_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec for High precision odometer",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_17": {
            "type": "string",
            "value": "0x740",
            "desc": "seat belt status",
            "rules": {
                "max_length": 5000
            }
        },
        "param_17_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_18": {
            "type": "string",
            "value": "0x6A4",
            "desc": "Lane departure Imminent left",
            "rules": {
                "max_length": 5000
            }
        },
        "param_18_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_19": {
            "type": "string",
            "value": "0x6A5",
            "desc": "Lane departure Imminent right",
            "rules": {
                "max_length": 5000
            }
        },
        "param_19_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_20": {
            "type": "string",
            "value": "0xDED",
            "desc": "Lane departure left",
            "rules": {
                "max_length": 5000
            }
        },
        "param_20_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_21": {
            "type": "string",
            "value": "0xDEE",
            "desc": "Lane departure right",
            "rules": {
                "max_length": 5000
            }
        },
        "param_21_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_22": {
            "type": "string",
            "value": "0x1FC7",
            "desc": "Right side lane departure optical warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_22_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_23": {
            "type": "string",
            "value": "0x1FC8",
            "desc": "Left side lane departure optical warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_23_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_24": {
            "type": "string",
            "value": "0x1FC9",
            "desc": "Right side lane departure Acoustical warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_24_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_25": {
            "type": "string",
            "value": "0x1FCA",
            "desc": "Left side lane departure Acoustical warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_25_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_26": {
            "type": "string",
            "value": "0x2615",
            "desc": "Right side lane departure Haptic warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_26_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_27": {
            "type": "string",
            "value": "0x2616",
            "desc": "Left side lane departure Haptic warning",
            "rules": {
                "max_length": 5000
            }
        },
        "param_27_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_28": {
            "type": "string",
            "value": "0x1D34",
            "desc": "Left Distance to lane mark",
            "rules": {
                "max_length": 5000
            }
        },
        "param_28_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_29": {
            "type": "string",
            "value": "0x1D35",
            "desc": "Right distance to lane mark",
            "rules": {
                "max_length": 5000
            }
        },
        "param_29_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_30": {
            "type": "string",
            "value": "0x6A6",
            "desc": "Lane departure Indication enable status",
            "rules": {
                "max_length": 5000
            }
        },
        "param_30_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_31": {
            "type": "string",
            "value": "0x1FCB",
            "desc": "Left wheel lane departure distance",
            "rules": {
                "max_length": 5000
            }
        },
        "param_31_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_32": {
            "type": "string",
            "value": "0x1FCC",
            "desc": "Right wheel lane departure distance",
            "rules": {
                "max_length": 5000
            }
        },
        "param_32_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        },
        "param_33": {
            "type": "string",
            "value": "0x1FCD",
            "desc": "Left departure warning system state",
            "rules": {
                "max_length": 5000
            }
        },
        "param_33_freq": {
            "type": "int",
            "value": 500,
            "desc": "update rate in msec",
            "rules": {
                "min": 100,
                "max": 1000
            }
        }
    },
    "OBDII_update_rates": {
        "speed": {
            "type": "int",
            "value": 1000,
            "desc": "speed update ms",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "rpm": {
            "type": "int",
            "value": 1000,
            "desc": "rpm update ms",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "Adc_value": {
            "type": "int",
            "value": 1000,
            "desc": "adc value update ms",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "Throttle_position": {
            "type": "int",
            "value": 1000,
            "desc": "throttle position ms",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "Coolant_Temperature": {
            "type": "int",
            "value": 30000,
            "desc": "coolant temp ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "fuel_level": {
            "type": "int",
            "value": 30000,
            "desc": "fuel level ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "Engine_On_time": {
            "type": "int",
            "value": 30000,
            "desc": "engine on time ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "distence_since_code_cleared": {
            "type": "int",
            "value": 30000,
            "desc": "distance since code cleared ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "fuel_system_status": {
            "type": "int",
            "value": 30000,
            "desc": "fuel system status ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "MAF_air_flow_rate": {
            "type": "int",
            "value": 30000,
            "desc": "MAF air flow ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "Absolute_Barometric_Pressure": {
            "type": "int",
            "value": 30000,
            "desc": "barometric pressure ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "Ambient_air_temperature": {
            "type": "int",
            "value": 30000,
            "desc": "ambient air temp ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param1": {
            "type": "int",
            "value": 30000,
            "desc": "prop param1 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param2": {
            "type": "int",
            "value": 30000,
            "desc": "prop param2 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param3": {
            "type": "int",
            "value": 30000,
            "desc": "prop param3 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param4": {
            "type": "int",
            "value": 30000,
            "desc": "prop param4 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param5": {
            "type": "int",
            "value": 30000,
            "desc": "prop param5 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param6": {
            "type": "int",
            "value": 30000,
            "desc": "prop param6 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param7": {
            "type": "int",
            "value": 30000,
            "desc": "prop param7 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param8": {
            "type": "int",
            "value": 30000,
            "desc": "prop param8 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param9": {
            "type": "int",
            "value": 30000,
            "desc": "prop param9 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param10": {
            "type": "int",
            "value": 30000,
            "desc": "prop param10 ms",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        }
    },
    "list_of_prop_canid": {
        "enabled": {
            "type": "bool",
            "value": false,
            "desc": "prop can id enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "prop_param1": {
            "type": "int",
            "value": 0,
            "desc": "prop can id1",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param2": {
            "type": "int",
            "value": 0,
            "desc": "prop can id2",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param3": {
            "type": "int",
            "value": 0,
            "desc": "prop can id3",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param4": {
            "type": "int",
            "value": 0,
            "desc": "prop can id4",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param5": {
            "type": "int",
            "value": 0,
            "desc": "prop can id5",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param6": {
            "type": "int",
            "value": 0,
            "desc": "prop can id6",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param7": {
            "type": "int",
            "value": 0,
            "desc": "prop can id7",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param8": {
            "type": "int",
            "value": 0,
            "desc": "prop can id8",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param9": {
            "type": "int",
            "value": 0,
            "desc": "prop can id9",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        },
        "prop_param10": {
            "type": "int",
            "value": 0,
            "desc": "prop can id10",
            "rules": {
                "min": 0,
                "max": 1000000
            }
        }
    },
    "aws_iot_publish": {
        "enabled": {
            "type": "string",
            "value": "false",
            "desc": "decides whether to send gps updates to cloud or not",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "publish_freq_in_secs": {
            "type": "int",
            "value": 10,
            "desc": "publish frequency secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        }
    },
    "vm": {
        "enabled": {
            "type": "string",
            "value": "true",
            "desc": "to tune virtual memory of linuz",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "dirty_writeback_centisecs": {
            "type": "int",
            "value": 100,
            "desc": "Post this time linux shall start writeback flush on dirty cache. 100 centisecond = 1 sec",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "dirty_expire_centisecs": {
            "type": "int",
            "value": 100,
            "desc": "Post this time linux starts fore flush on dirty cache.",
            "rules": {
                "min": 0,
                "max": 100000
            }
        }
    },
    "gps": {
        "gps_fail_counter": {
            "type": "int",
            "value": 5,
            "desc": "in minutes, post this gps recover command is run",
            "rules": {
                "min": 5,
                "max": 100000
            }
        },
        "lat_long_retention_duration": {
            "type": "int",
            "value": 5,
            "desc": "in secs,frequency to store the lat long in gps_cache.json",
            "rules": {
                "min": 1,
                "max": 100000
            }
        },
        "nmea_log_enabled": {
            "type": "bool",
            "value": false,
            "desc": "NMEA log enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "agnss": {
        "enabled": {
            "type": "bool",
            "value": false,
            "desc": "Quectel module specific feature, should be true for Quectel modem",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "no_of_epofiles": {
            "type": "int",
            "value": 4,
            "desc": "number of epofiles to be downloaded",
            "rules": {
                "min": 1,
                "max": 4
            }
        },
        "cold_start_enabled": {
            "type": "bool",
            "value": false,
            "desc": "cold start enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "cold_start_thread_sleep_time": {
            "type": "int",
            "value": 600,
            "desc": "Only for cold start testing, value is cold start duration in secs",
            "rules": {
                "min": 600,
                "max": 86400
            }
        }
    },
    "uploader_settings": {
        "conn_timeout_alert": {
            "type": "int",
            "value": 90,
            "desc": "connection timeout alert secs",
            "rules": {
                "min": 60,
                "max": 120
            }
        },
        "max_timeout_alert": {
            "type": "int",
            "value": 120,
            "desc": "max timeout alert secs",
            "rules": {
                "min": 100,
                "max": 200
            }
        },
        "vod_timeout_hrs": {
            "type": "int",
            "value": 72,
            "desc": "vod timeout hours",
            "rules": {
                "min": 1,
                "max": 720
            }
        },
        "curl_init_once_flag": {
            "type": "int",
            "value": 1,
            "desc": "Flag to do curl init once per process or per thread",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        }
    },
    "upload_settings": {
        "observation_frequency": {
            "type": "int",
            "value": 10,
            "desc": "observation frequency integer",
            "rules": {
                "min": 1,
                "max": 30
            }
        }
    },
    "live_streaming": {
        "enabled": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable live streaming ",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        },
        "enc_type": {
            "type": "int",
            "value": 2,
            "desc": "livestream track type (only applicable for krait)",
            "rules": {
                "min": 0,
                "max": 3
            }
        },
        "fps": {
            "type": "int",
            "value": 10,
            "desc": "fps at which livestreaming will happen",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "width": {
            "type": "int",
            "value": 640,
            "desc": "width of livesteaming video",
            "rules": {
                "min": 320,
                "max": 1920
            }
        },
        "height": {
            "type": "int",
            "value": 360,
            "desc": "height of livestreaming video",
            "rules": {
                "min": 180,
                "max": 1080
            }
        },
        "bitrate": {
            "type": "int",
            "value": 512000,
            "desc": "stream bitrate bps",
            "rules": {
                "min": 200000,
                "max": 6000000
            }
        },
        "iframeinterval": {
            "type": "int",
            "value": 10,
            "desc": "intervals at which key frame will be inserted in livestreaming pipeline",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "outward_stream_start_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_outward_start.wav",
            "desc": "audio file to be played when livestreaming for outward camera starts",
            "rules": {
                "max_length": 5000
            }
        },
        "outward_stream_end_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_outward_end.wav",
            "desc": "audio file to be played when livestreaming for outward camera ends",
            "rules": {
                "max_length": 5000
            }
        },
        "inward_stream_start_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_start.wav",
            "desc": "audio file to be played when livestreaming for inward camera starts",
            "rules": {
                "max_length": 5000
            }
        },
        "inward_stream_end_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_end.wav",
            "desc": "audio file to be played when livestreaming for inward camera ends",
            "rules": {
                "max_length": 5000
            }
        },
        "dual_stream_start_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/dual_live_streaming_start.wav",
            "desc": "dual start audio",
            "rules": {
                "max_length": 5000
            }
        },
        "dual_stream_end_file": {
            "type": "string",
            "value": "/home/ubuntu/autocam/audio/nd_debug2/dual_live_streaming_end.wav",
            "desc": "dual end audio",
            "rules": {
                "max_length": 5000
            }
        },
        "audio_notification": {
            "type": "string",
            "value": "false",
            "desc": "to decide whether audio should be played or not for livestreaming event",
            "rules": {
                "allowed_values": [
                    "true",
                    "false"
                ]
            }
        }
    },
    "hdmaps_mode": {
        "metadata": {
            "type": "bool",
            "value": false,
            "desc": "hdmaps metadata",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "enable": {
            "type": "string",
            "value": "false",
            "desc": "hdmaps enable",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "imu_data": {
            "type": "string",
            "value": "false",
            "desc": "hdmaps imu data",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        }
    },
    "ublox": {
        "enabled": {
            "type": "string",
            "value": "false",
            "desc": "to enable ublox protocol mode (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "led_indication": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable ublox led indication (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "send_ubx_cfg_msgs": {
            "type": "string",
            "value": "true",
            "desc": "(not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "log_ubx_msgs": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable logging ubx messages (not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "save_ubx_cfg": {
            "type": "string",
            "value": "true",
            "desc": "(not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "disable_nmea_msgs": {
            "type": "string",
            "value": "true",
            "desc": "flag to disable NMEA messages (not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "enable_GPS": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable GPS (not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "enable_SBAS": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable SBAS layer on top of gps to enhance reliability, accuracy and safety (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "enable_Galileo": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable european galilieo satellites   (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "enable_BeiDou": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable china satellites navigation system (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "enable_GLONASS": {
            "type": "string",
            "value": "true",
            "desc": "flag to enable russian satellites navigation system (not using)",
            "rules": {
                "allowed_values": [
                    "true"
                ]
            }
        },
        "enable_IMES": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable gps performance inside building or smaller area in japan (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        },
        "enable_QZSS": {
            "type": "string",
            "value": "false",
            "desc": "flag to enable japanese satellite navigation system  (not using)",
            "rules": {
                "allowed_values": [
                    "false"
                ]
            }
        }
    },
    "healthstats": {
        "process_info_secs": {
            "type": "int",
            "value": 5,
            "desc": "process info sampling secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "cpu_gpu_info_secs": {
            "type": "int",
            "value": 5,
            "desc": "cpu gpu info sampling secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "videohealthstats_secs": {
            "type": "int",
            "value": 600,
            "desc": "video health upload secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "healthstats_secs": {
            "type": "int",
            "value": 60,
            "desc": "healthstats sampling secs",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "db_version": {
            "type": "int",
            "value": 2,
            "desc": "db version",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "health_analytics": {
            "type": "bool",
            "value": true,
            "desc": "health analytics enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "diagnostic": {
        "sdcard_diag_interval_time": {
            "type": "int",
            "value": 60,
            "desc": "SDCARD Interval time in Seconds",
            "rules": {
                "allowed_values": [
                    60,
                    300,
                    600,
                    900,
                    6000,
                    86400
                ]
            }
        },
        "sdcard_diag_start_time": {
            "type": "int",
            "value": 30,
            "desc": "SDCARD start time in Seconds",
            "rules": {
                "min": 0,
                "max": 60
            }
        },
        "enable_all_time_sdcard_error_state_log": {
            "type": "bool",
            "value": false,
            "desc": "If enabled , checks if the SDCARD Entries are captured in Diagnostic DB.",
            "rules": {
                "allowed_values": [
                    false,
                    true
                ]
            }
        },
        "waf_enabled": {
            "type": "int",
            "value": 1,
            "desc": "0 - disabled, 1 - enabled",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "waf_diag_interval_time": {
            "type": "int",
            "value": 30,
            "desc": "waf Check running frequency interval in minutes",
            "rules": {
                "min": 1,
                "max": 1440
            }
        },
        "log_overlay": {
            "type": "bool",
            "value": true,
            "desc": "false - disabled, true - enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "ext_cam": {
        "ch1_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH1 streaming",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch1_framerate": {
            "type": "int",
            "value": 25,
            "desc": "CH1 framerate",
            "rules": {
                "allowed_values": [
                    25,
                    30
                ]
            }
        },
        "ch2_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH2 streaming",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch2_framerate": {
            "type": "int",
            "value": 25,
            "desc": "CH2 framerate",
            "rules": {
                "allowed_values": [
                    25,
                    30
                ]
            }
        },
        "ch3_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH3 streaming",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch3_framerate": {
            "type": "int",
            "value": 25,
            "desc": "CH3 framerate",
            "rules": {
                "allowed_values": [
                    25,
                    30
                ]
            }
        },
        "ch4_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH4 streaming",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch4_framerate": {
            "type": "int",
            "value": 25,
            "desc": "CH4 framerate",
            "rules": {
                "allowed_values": [
                    25,
                    30
                ]
            }
        }
    },
    "ext_cam_audio": {
        "ch1_audio_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH1 audio",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch2_audio_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH2 audio",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch3_audio_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH3 audio",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ch4_audio_enabled": {
            "type": "bool",
            "value": false,
            "desc": "CH4 audio",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "ext_cam_time": {
        "time_zone": {
            "type": "int",
            "value": -800,
            "desc": "timezone offset",
            "rules": {
                "min": -1200,
                "max": 1400
            }
        },
        "video_timestamp_enable": {
            "type": "bool",
            "value": false,
            "desc": "video timestamp enable",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "ext_cam_settings": {
        "retry_count_vod": {
            "type": "int",
            "value": 2,
            "desc": "vod retry count",
            "rules": {
                "min": 1,
                "max": 10
            }
        },
        "sleep_retry": {
            "type": "int",
            "value": 10,
            "desc": "sleep between vod retries secs",
            "rules": {
                "min": 1,
                "max": 300
            }
        },
        "max_pull_time": {
            "type": "int",
            "value": 600,
            "desc": "max mdvr file pull secs",
            "rules": {
                "min": 60,
                "max": 3600
            }
        },
        "time_offset_millis": {
            "type": "int",
            "value": -2000,
            "desc": "video time offset ms",
            "rules": {
                "min": -10000,
                "max": 10000
            }
        },
        "file_empty_max_retry_count": {
            "type": "int",
            "value": 30,
            "desc": "empty file max retry",
            "rules": {
                "min": 1,
                "max": 100
            }
        },
        "mdvr_firmware_upgrade": {
            "type": "bool",
            "value": false,
            "desc": "mdvr firmware upgrade",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "save_ext_cam_files_in_dhub": {
            "type": "bool",
            "value": true,
            "desc": "save hub session files locally",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "mdvr_health_check_duration": {
            "type": "int",
            "value": 1,
            "desc": "health check duration mins",
            "rules": {
                "min": 1,
                "max": 60
            }
        },
        "mdvr_low_power_wakeup": {
            "type": "bool",
            "value": true,
            "desc": "mdvr low power wakeup",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ext_cam_video_request_duration": {
            "type": "int",
            "value": 60,
            "desc": "ext cam video request duration secs",
            "rules": {
                "min": 30,
                "max": 300
            }
        },
        "rgb_analysis": {
            "type": "bool",
            "value": false,
            "desc": "rgb analysis",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ext_cam_rgb_analysis_timer": {
            "type": "int",
            "value": 180,
            "desc": "rgb analysis period mins",
            "rules": {
                "min": 60,
                "max": 3600
            }
        },
        "firmware_upgrade_speed": {
            "type": "int",
            "value": 30,
            "desc": "firmware upgrade minimum speed",
            "rules": {
                "min": 30,
                "max": 100
            }
        },
        "gen3_sta_firmware_version": {
            "type": "string",
            "value": "S25122501.35848",
            "desc": "expected mdvr firmware gen3",
            "rules": {
                "max_length": 5000
            }
        },
        "sta_timeout": {
            "type": "int",
            "value": 600,
            "desc": "dhub station mode timeout secs",
            "rules": {
                "min": 180,
                "max": 900
            }
        },
        "screen_login_enabled": {
            "type": "bool",
            "value": true,
            "desc": "screen login enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "screen_login_password": {
            "type": "string",
            "value": "DHUB@123",
            "desc": "screen login password",
            "rules": {
                "max_length": 5000
            }
        },
        "auto_configuration": {
            "type": "string",
            "value": "disable",
            "desc": "auto configuration flag",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "max_db_limit": {
            "type": "int",
            "value": 10000,
            "desc": "max ext cam db entries",
            "rules": {
                "min": 1000,
                "max": 20000
            }
        },
        "max_num_pull_threads": {
            "type": "int",
            "value": 2,
            "desc": "pull threads count",
            "rules": {
                "min": 1,
                "max": 4
            }
        },
        "login_password": {
            "type": "string",
            "value": "DHUB@123",
            "desc": "login password",
            "rules": {
                "max_length": 5000
            }
        },
        "dhub_server_ip": {
            "type": "string",
            "value": "10.10.10.254",
            "desc": "dhub server ip",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "NightMode": {
        "ir_mode": {
            "type": "string",
            "value": "auto",
            "desc": "IR mode",
            "rules": {
                "max_length": 5000
            }
        },
        "max_lux": {
            "type": "int",
            "value": 60,
            "desc": "max lux",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "min_lux": {
            "type": "int",
            "value": 40,
            "desc": "min lux",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "irled_on_hr": {
            "type": "int",
            "value": 16,
            "desc": "turn on IRLED at 4pm local time - changed to adjust winter",
            "rules": {
                "min": 0,
                "max": 23
            }
        },
        "irled_off_hr": {
            "type": "int",
            "value": 7,
            "desc": "turn off IRLED at 7am local time",
            "rules": {
                "min": 0,
                "max": 23
            }
        },
        "irled_on_intensity": {
            "type": "int",
            "value": 30,
            "desc": "IRLED intensity percent",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "irled_level": {
            "type": "int",
            "value": 1,
            "desc": "brightness level for IR LEDs. Accepted levels: 0,1,2,3 only",
            "rules": {
            "allowed_values": [
                0,
                1,
                2,
                3
            ]
            }
        },
        "analytics_control_ir_led": {
            "type": "string",
            "value": "disable",
            "desc": "analytics control ir led",
            "rules": {
                "allowed_values": [
                    "enable",
                    "disable"
                ]
            }
        },
        "photodiode_enable": {
            "type": "bool",
            "value": true,
            "desc": "photodiode control enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "log": {
        "log_freq_bloating": {
            "type": "int",
            "value": 500,
            "desc": "log freq bloating",
            "rules": {
                "min": 10,
                "max": 1000
            }
        },
        "enable_non_critical": {
            "type": "bool",
            "value": false,
            "desc": "enable or disable detailed logs for upload",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "non_critical_log_retain_period": {
            "type": "int",
            "value": 30,
            "desc": "retain period non critical days",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "syslog_retain_period": {
            "type": "int",
            "value": 15,
            "desc": "retain period syslog days",
            "rules": {
                "min": 1,
                "max": 30
            }
        },
        "syslog_limit": {
            "type": "int",
            "value": 200,
            "desc": "syslog limit",
            "rules": {
                "min": 1,
                "max": 400
            }
        },
        "enable_age_based_deletion": {
            "type": "bool",
            "value": false,
            "desc": "age based deletion",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "enable_syslog": {
            "type": "bool",
            "value": false,
            "desc": "syslog enabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "service_based_critical_log_filter": {
            "type": "bool",
            "value": false,
            "desc": "service critical log filter",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "conn_mgr_config": {
        "modem_log_enabled": {
            "type": "bool",
            "value": false,
            "desc": "Enable or disable modem logging",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "min_modem_log_capture_duration": {
            "type": "int",
            "value": 10,
            "desc": "Value should be 0, only if modem log need to capture continuously for short time, otherwise valid value log will be stored for atleast (<val> * 45)sec",
            "rules": {
                "min": 0,
                "max": 60
            }
        },
        "wcdma_band_enabled": {
            "type": "bool",
            "value": false,
            "desc": "If enable, WCDMA band will be enabled based on region, otherwise LTE only mode will be supported.",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ipv6_session_disabled": {
            "type": "bool",
            "value": false,
            "desc": "If enable, IPv6 will be disabled, IPv4 session only will be requested. Otherwise both IPv4 and IPv6 will be supported.",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "modem_reset": {
            "type": "bool",
            "value": false,
            "desc": "If true when ip is present but ping is not working modem reset will be initiated otherwise only service restart will be triggerred",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "siginfo_duration": {
            "type": "int",
            "value": 2,
            "desc": "duration for updating signal info. The actual duration value * 45secs",
            "rules": {
                "min": 1,
                "max": 1000
            }
        }
    },
    "ble_alert": {
        "enabled": {
            "type": "int",
            "value": 0,
            "desc": "ble alert enabled",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "uuid_alert_tag": {
            "type": "string",
            "value": "4E443A55414C5254",
            "desc": "alert tag hex",
            "rules": {
                "max_length": 5000
            }
        },
        "long_press_tag": {
            "type": "string",
            "value": "4E443A4C4F4E47",
            "desc": "hex tag prefix for long press packets",
            "rules": {
                "max_length": 5000
            }
        },
        "ble_vendor": {
            "type": "string",
            "value": "MOKO",
            "desc": "ble vendor",
            "rules": {
                "max_length": 5000
            }
        },
        "low_battery_threshold_percentage": {
            "type": "int",
            "value": 20,
            "desc": "low battery threshold % for raising critical info",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "keep_alive_miss_interval": {
            "type": "int",
            "value": 3600,
            "desc": "seconds without keep alive before device considered disconnected",
            "rules": {
                "min": 0,
                "max": 86400
            }
        },
        "auto_pairing": {
            "type": "bool",
            "value": false,
            "desc": "enable auto pairing for BLE alert devices",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        }
    },
    "nd_bt": {
        "scan": {
            "type": "int",
            "value": 1,
            "desc": "scan mode (1 - active, 0 - passive)",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "interval": {
            "type": "int",
            "value": 30,
            "desc": "scan interval units (0x001E)",
            "rules": {
                "min": 0,
                "max": 300
            }
        },
        "window": {
            "type": "int",
            "value": 10,
            "desc": "scan window units (0x000A)",
            "rules": {
                "min": 0,
                "max": 300
            }
        },
        "addr": {
            "type": "int",
            "value": 0,
            "desc": "address type",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "scan_time_btw_char_ops": {
            "type": "int",
            "value": 0,
            "desc": "time between char ops secs",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "reload_stack_interval_minutes": {
            "type": "int",
            "value": 0,
            "desc": "reload bt stack interval mins",
            "rules": {
                "min": 0,
                "max": 2880
            }
        },
        "relaxed_window": {
            "type": "int",
            "value": 48,
            "desc": "relaxed window",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "relaxed_interval": {
            "type": "int",
            "value": 96,
            "desc": "relaxed interval",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "relaxed_scan_max_timeout": {
            "type": "int",
            "value": 8,
            "desc": "relaxed scan max timeout secs",
            "rules": {
                "min": 0,
                "max": 100000
            }
        },
        "nearby_devices_max_size": {
            "type": "int",
            "value": 50,
            "desc": "maximum nearby device entries to track",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "nearby_alert_beacon_max_size": {
            "type": "int",
            "value": 20,
            "desc": "maximum alert beacon entries to track",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "adsm_enabled": {
            "type": "int",
            "value": 1,
            "desc": "Enable ADSM state persistence for crash recovery (0=disabled, 1=enabled)",
            "rules": {
                "allowed_values": [
                    0,
                    1
                ]
            }
        },
        "adsm_recovery_window_duration": {
            "type": "int",
            "value": 300,
            "desc": "ADSM recovery window duration in seconds",
            "rules": {
                "min": 60,
                "max": 600
            }
        },
        "adsm_downtime_duration": {
            "type": "int",
            "value": 300,
            "desc": "ADSM downtime duration in seconds",
            "rules": {
                "min": 60,
                "max": 600
            }
        },
        "rate_limiter_write_char_seconds": {
            "type": "int",
            "value": 60,
            "desc": "seconds between characteristic writes",
            "rules": {
                "min": 0,
                "max": 100000
            }
        }
    },
    "vh_params": {
        "bucket0_count": {
            "type": "int",
            "value": 0,
            "desc": "bucket 0 param count",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "bucket1_count": {
            "type": "int",
            "value": 0,
            "desc": "bucket 1 param count",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "bucket2_count": {
            "type": "int",
            "value": 0,
            "desc": "bucket 2 param count",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "bucket0_param1": {
            "type": "string",
            "value": "engine_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param1": {
            "type": "string",
            "value": "battery_voltage",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param1": {
            "type": "string",
            "value": "fuel_level_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param2": {
            "type": "string",
            "value": "battery_state_of_charge_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "buvket0_param3": {
            "type": "string",
            "value": "battery_voltage",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param4": {
            "type": "string",
            "value": "battery_potential_power_input",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param5": {
            "type": "string",
            "value": "engine_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param6": {
            "type": "string",
            "value": "engine_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param7": {
            "type": "string",
            "value": "engine_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param8": {
            "type": "string",
            "value": "coolant_level_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param9": {
            "type": "string",
            "value": "coolant_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param10": {
            "type": "string",
            "value": "engine_load_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param11": {
            "type": "string",
            "value": "transmission_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param12": {
            "type": "string",
            "value": "transmission_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param13": {
            "type": "string",
            "value": "transmission_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param14": {
            "type": "string",
            "value": "transmission_oil_life_remaining",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param15": {
            "type": "string",
            "value": "transmission_service_indicator",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param16": {
            "type": "string",
            "value": "washer_fluid_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param17": {
            "type": "string",
            "value": "aftertreatment1_diesel_exhaust_fluid_tank_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param18": {
            "type": "string",
            "value": "aftertreatment1_regeneration_status",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param19": {
            "type": "string",
            "value": "tire_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param20": {
            "type": "string",
            "value": "tire_pressure_threshold_detection",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param21": {
            "type": "string",
            "value": "tire_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param22": {
            "type": "string",
            "value": "dummy1",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param23": {
            "type": "string",
            "value": "dummy2",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param24": {
            "type": "string",
            "value": "dummy3",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket0_param25": {
            "type": "string",
            "value": "dummy4",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param2": {
            "type": "string",
            "value": "battery_state_of_charge_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param3": {
            "type": "string",
            "value": "battery_voltage",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param4": {
            "type": "string",
            "value": "battery_potential_power_input",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param5": {
            "type": "string",
            "value": "engine_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param6": {
            "type": "string",
            "value": "engine_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param7": {
            "type": "string",
            "value": "engine_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param8": {
            "type": "string",
            "value": "coolant_level_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param9": {
            "type": "string",
            "value": "coolant_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param10": {
            "type": "string",
            "value": "engine_load_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param11": {
            "type": "string",
            "value": "transmission_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param12": {
            "type": "string",
            "value": "transmission_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param13": {
            "type": "string",
            "value": "transmission_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param14": {
            "type": "string",
            "value": "transmission_oil_life_remaining",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param15": {
            "type": "string",
            "value": "transmission_service_indicator",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param16": {
            "type": "string",
            "value": "washer_fluid_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param17": {
            "type": "string",
            "value": "aftertreatment1_diesel_exhaust_fluid_tank_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param18": {
            "type": "string",
            "value": "aftertreatment1_regeneration_status",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param19": {
            "type": "string",
            "value": "tire_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param20": {
            "type": "string",
            "value": "tire_pressure_threshold_detection",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param21": {
            "type": "string",
            "value": "tire_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param22": {
            "type": "string",
            "value": "dummy1",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param23": {
            "type": "string",
            "value": "dummy2",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param24": {
            "type": "string",
            "value": "dummy3",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket1_param25": {
            "type": "string",
            "value": "dummy4",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param2": {
            "type": "string",
            "value": "battery_state_of_charge_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param3": {
            "type": "string",
            "value": "battery_voltage",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param4": {
            "type": "string",
            "value": "battery_potential_power_input",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param5": {
            "type": "string",
            "value": "engine_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param6": {
            "type": "string",
            "value": "engine_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param7": {
            "type": "string",
            "value": "engine_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param8": {
            "type": "string",
            "value": "coolant_level_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param9": {
            "type": "string",
            "value": "coolant_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param10": {
            "type": "string",
            "value": "engine_load_percent",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param11": {
            "type": "string",
            "value": "transmission_oil_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param12": {
            "type": "string",
            "value": "transmission_oil_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param13": {
            "type": "string",
            "value": "transmission_oil_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param14": {
            "type": "string",
            "value": "transmission_oil_life_remaining",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param15": {
            "type": "string",
            "value": "transmission_service_indicator",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param16": {
            "type": "string",
            "value": "washer_fluid_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param17": {
            "type": "string",
            "value": "aftertreatment1_diesel_exhaust_fluid_tank_level",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param18": {
            "type": "string",
            "value": "aftertreatment1_regeneration_status",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param19": {
            "type": "string",
            "value": "tire_pressure",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param20": {
            "type": "string",
            "value": "tire_pressure_threshold_detection",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param21": {
            "type": "string",
            "value": "tire_temperature",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param22": {
            "type": "string",
            "value": "dummy1",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param23": {
            "type": "string",
            "value": "dummy2",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param24": {
            "type": "string",
            "value": "dummy3",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        },
        "bucket2_param25": {
            "type": "string",
            "value": "dummy4",
            "desc": "bucket and paramater name",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "master_vehicle_health_params": {
        "count": {
            "type": "int",
            "value": 18,
            "desc": "number of health parameters",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "param1": {
            "type": "string",
            "value": "fuel_level_percent",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param1_loc": {
            "type": "string",
            "value": "Fuel,1",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param2": {
            "type": "string",
            "value": "battery_state_of_charge_percent",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param2_loc": {
            "type": "string",
            "value": "Electric,6",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param3": {
            "type": "string",
            "value": "battery_voltage",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param3_loc": {
            "type": "string",
            "value": "Data,9",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param4": {
            "type": "string",
            "value": "battery_potential_power_input",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param4_loc": {
            "type": "string",
            "value": "Electric,1",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param5": {
            "type": "string",
            "value": "engine_oil_level",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param5_loc": {
            "type": "string",
            "value": "Engine,15",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param6": {
            "type": "string",
            "value": "engine_oil_temperature",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param6_loc": {
            "type": "string",
            "value": "Engine,11",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param7": {
            "type": "string",
            "value": "engine_oil_pressure",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param7_loc": {
            "type": "string",
            "value": "Engine,1",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param8": {
            "type": "string",
            "value": "coolant_level_percent",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param8_loc": {
            "type": "string",
            "value": "Engine,16",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param9": {
            "type": "string",
            "value": "coolant_temperature",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param9_loc": {
            "type": "string",
            "value": "Engine,10",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param10": {
            "type": "string",
            "value": "engine_load_percent",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param10_loc": {
            "type": "string",
            "value": "Engine,6",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param11": {
            "type": "string",
            "value": "transmission_oil_level",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param11_loc": {
            "type": "string",
            "value": "Transmission,7",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param12": {
            "type": "string",
            "value": "transmission_oil_temperature",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param12_loc": {
            "type": "string",
            "value": "Transmission,4",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param13": {
            "type": "string",
            "value": "transmission_oil_pressure",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param13_loc": {
            "type": "string",
            "value": "Transmission,8",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param14": {
            "type": "string",
            "value": "transmission_oil_life_remaining",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param14_loc": {
            "type": "string",
            "value": "Transmission,9",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param15": {
            "type": "string",
            "value": "transmission_service_indicator",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param15_loc": {
            "type": "string",
            "value": "Transmission,10",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param16": {
            "type": "string",
            "value": "washer_fluid_level",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param16_loc": {
            "type": "string",
            "value": "Driver,22",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param17": {
            "type": "string",
            "value": "aftertreatment1_diesel_exhaust_fluid_tank_level",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param17_loc": {
            "type": "string",
            "value": "Emissions,13",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param18": {
            "type": "string",
            "value": "aftertreatment1_regeneration_status",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param18_loc": {
            "type": "string",
            "value": "Emissions,20",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param_count": {
            "type": "int",
            "value": 3,
            "desc": "number of tire parameters",
            "rules": {
                "min": 0,
                "max": 1000
            }
        },
        "tire_param1": {
            "type": "string",
            "value": "tire_pressure",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param1_loc": {
            "type": "string",
            "value": "Tires,2",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param2": {
            "type": "string",
            "value": "tire_pressure_threshold_detection",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param2_loc": {
            "type": "string",
            "value": "Tires,6",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param3": {
            "type": "string",
            "value": "tire_temperature",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param3_loc": {
            "type": "string",
            "value": "Tires,3",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        },
        "param19": {
            "type": "string",
            "value": "dummy1",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param19_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param20": {
            "type": "string",
            "value": "dummy2",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param20_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param21": {
            "type": "string",
            "value": "dummy3",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param21_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param22": {
            "type": "string",
            "value": "dummy4",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param22_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param23": {
            "type": "string",
            "value": "dummy5",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param23_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param24": {
            "type": "string",
            "value": "dummy6",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param24_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param25": {
            "type": "string",
            "value": "dummy7",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "param25_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param4": {
            "type": "string",
            "value": "dummy8",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param4_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param5": {
            "type": "string",
            "value": "dummy9",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param5_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param6": {
            "type": "string",
            "value": "dummy10",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param6_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param7": {
            "type": "string",
            "value": "dummy11",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param7_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param8": {
            "type": "string",
            "value": "dummy12",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param8_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param9": {
            "type": "string",
            "value": "dummy13",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param9_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param10": {
            "type": "string",
            "value": "dummy14",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        },
        "tire_param10_loc": {
            "type": "string",
            "value": "",
            "desc": "placholder for new parameters",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "VBUS_common_config": {
        "count": {
            "type": "int",
            "value": 1,
            "desc": "No of newly added VBUS parameters",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "param1": {
            "type": "string",
            "value": "rpm",
            "desc": "param name",
            "rules": {
                "max_length": 5000
            }
        },
        "param1_loc": {
            "type": "string",
            "value": "Data,2",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "OBD_Installer_app": {
        "count": {
            "type": "int",
            "value": 1,
            "desc": "No of additional parameters to be sent to Installer App",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "param1_loc": {
            "type": "string",
            "value": "Data,2",
            "desc": "param location",
            "rules": {
                "max_length": 5000
            }
        }
    },
    "apm": {
        "apm_motion_detection": {
            "type": "bool",
            "value": false,
            "desc": "motion detection feature is enabled / disabled",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_imu_enable": {
            "type": "bool",
            "value": true,
            "desc": "Use IMU to detect motion and update pseudo ignition",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_igns_enable": {
            "type": "bool",
            "value": true,
            "desc": "Use physical ignition to update pseudo ignition",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_supercap_enable": {
            "type": "bool",
            "value": true,
            "desc": "Detect the status of super cap",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_wom_enable": {
            "type": "bool",
            "value": false,
            "desc": "wake on motion feature",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_can_enable": {
            "type": "bool",
            "value": false,
            "desc": "use CAN to detect motion. Feature not supported now",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_gps_enable": {
            "type": "bool",
            "value": true,
            "desc": "Use GPS data to detect motion and update pseudo ignition",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "apm_crank_volt_enable": {
            "type": "bool",
            "value": true,
            "desc": "Use Power voltage data to detect motion and update pseudo ignition",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "gps_accuracy": {
            "type": "float",
            "value": 10.0,
            "desc": "minimum gps accuracy to consider it as valid",
            "rules": {
                "min": 0.0,
                "max": 100.0
            }
        },
        "gps_speed": {
            "type": "float",
            "value": 5.0,
            "desc": "gps speed threshold for engine/ignition on",
            "rules": {
                "min": 0.0,
                "max": 200.0
            }
        },
        "gps_speed_off": {
            "type": "float",
            "value": 2.0,
            "desc": "gps speed threshold for engine/ignition off",
            "rules": {
                "min": 0.0,
                "max": 200.0
            }
        },
        "imu_threshold": {
            "type": "float",
            "value": 0.25,
            "desc": "imu threshold is to check for device idle status",
            "rules": {
                "min": 0.0,
                "max": 160.0
            }
        },
        "imu_threshold_off": {
            "type": "float",
            "value": 0.20,
            "desc": "imu threshold off is to check for device engine off status",
            "rules": {
                "min": 0.0,
                "max": 160.0
            }
        },
        "vehicle_idle_time": {
            "type": "int",
            "value": 180,
            "desc": "vehicle idle time is set to 180 in second",
            "rules": {
                "min": 30,
                "max": 600
            }
        },
        "enable_ignition_based_wakeup": {
            "type": "bool",
            "value": true,
            "desc": "ignition interrupt configuration",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "supercap_threshold": {
            "type": "float",
            "value": 9.25,
            "desc": "supercap lower threshold volts",
            "rules": {
                "min": 0.0,
                "max": 17
            }
        },
        "supercap_threshold_off": {
            "type": "float",
            "value": 9.50,
            "desc": "supercap upper threshold volts",
            "rules": {
                "min": 0,
                "max": 17
            }
        },
        "supercap_threshold_24V": {
            "type": "float",
            "value": 9.25,
            "desc": "supercap lower threshold volts for 24V battery",
            "rules": {
                "min": 0,
                "max": 35
            }
        },
        "supercap_threshold_off_24V": {
            "type": "float",
            "value": 9.50,
            "desc": "supercap upper threshold volts for 24V battery",
            "rules": {
                "min": 0,
                "max": 35
            }
        },
        "imu_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "Use legacy to detect engine on status for imu",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "pwr_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "Use legacy to detect engine on status for power volt",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "gps_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "Use legacy to detect engine on status for gps",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "can_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "Use legacy to detect engine on status for can",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "ign_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "use legacy ignition feature for apm motion detection",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "pfi_use_legacy": {
            "type": "bool",
            "value": false,
            "desc": "use legacy pfi feature for apm motion detection",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "engine_voltage_threshold": {
            "type": "string",
            "value": "12.21,12.41,13.01,11.20,15.01,15.01",
            "desc": "voltage thresholds for power state changes (ENGINE OFF, ENGINE ON, IDLE, FUSION OFF, FUSION ON, FUSION IDLE)",
            "rules": {
                "max_length": 256
            }
        },
        "engine_voltage_threshold_24V": {
            "type": "string",
            "value": "25.21,25.81,27.01,21.20,32.01,32.01",
            "desc": "voltage thresholds for power state changes in 24V systems (ENGINE OFF, ENGINE ON, IDLE, FUSION OFF, FUSION ON, FUSION IDLE)",
            "rules": {
                "max_length": 256
            }
        },
        "can_rpm_threshold": {
            "type": "int",
            "value": 350,
            "desc": "can rpm threshold for engine on",
            "rules": {
                "min": 1,
                "max": 10000
            }
        },
        "can_rpm_off": {
            "type": "int",
            "value": 200,
            "desc": "can rpm threshold for engine off",
            "rules": {
                "min": 1,
                "max": 10000
            }
        },
        "adaptive_wom_trigger_count": {
            "type": "int",
            "value": 0,
            "desc": "number of wom triggers in misc wakeups for adaptive increase in wom threshold, 0 to disable",
            "rules": {
                "min": 0,
                "max": 10
            }
        },
        "easy_install": {
            "type": "int",
            "value": 0,
            "desc": "Ignition Less Driver-I Installation. 0 - Normal Installation, 1 - Easy Installation.",
            "rules": {
                "min": 0,
                "max": 100
            }
        },
        "engine_status_detection": {
            "type": "bool",
            "value": true,
            "desc": "Enable engine status detection based on IMU data",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "engine_imu_threshold":{
            "type": "float",
            "value": 0.03,
            "desc": "Threshold for Engine ON based on IMU virbation analysis",
            "rules": {
                "min": 0.0001,
                "max": 16.0000
            }
        }
    },
    "speed": {
        "out_of_idle_soak_timeout": {
            "type": "int",
            "value": 5,
            "desc": "out of idle soak timeout secs",
            "rules": {
                "min": 5,
                "max": 3600
            }
        },
        "invalid_speed_threshold": {
            "type": "float",
            "value": 5.0,
            "desc": "out of idle soak timeout secs",
            "rules": {
                "min": 2.0,
                "max": 3600.0
            }
        },
        "invalid_accuracy_threshold": {
            "type": "float",
            "value": 10.0,
            "desc": "out of idle soak timeout secs",
            "rules": {
                "min": 0.0,
                "max": 15.0
            }
        }
    },
    "cpu_scheduler": {
        "supercap_mode": {
            "type": "bool",
            "value": false,
            "desc": "To decrease cpu frequency and switch off fan when supercap is triggered",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "normal_mode": {
            "type": "bool",
            "value": false,
            "desc": "To configure cpu frequencies during a normal run",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "master_enable": {
            "type": "bool",
            "value": false,
            "desc": "true: enables the PowerStateController which maintains power and frequencies",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "lpw_mode": {
            "type": "bool",
            "value": false,
            "desc": "true: Allows to give custom frequencies for low Power mode(default will be used otherwise)",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "issue_state_mode": {
            "type": "bool",
            "value": false,
            "desc": "true: Allows to give custom frequencies for thermal throttling and bad battery(default will be used otherwise)",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "boost_mode": {
            "type": "bool",
            "value": false,
            "desc": "true: Allows to give custom frequencies for boost Power mode(default will be used otherwise)",
            "rules": {
                "allowed_values": [
                    true,
                    false
                ]
            }
        },
        "cpu_temp_throttle_threshold": {
            "type": "int",
            "value": 92,
            "desc": "decides at which temperature the device should stop frames to analytics",
            "rules": {
                "min": 75,
                "max": 105
            }
        },
        "hysteresis_margin": {
            "type": "int",
            "value": 7,
            "desc": "Decides the hysteresis from the throttling temperature to start sending frames to analytics again",
            "rules": {
                "min": 1,
                "max": 20
            }
        }
    }
}
