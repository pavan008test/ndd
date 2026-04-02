The L8B rated current is 600mA. But During cold boot up time due to calibration current consumption will go above 800mA.
So, the fix is to reduce target power for calibration during cold boot time.
Two changes are made in the BDF file. These changes will help to reduce the current consumption during the cold boot and should not cause any other problems
1)cold boot time cal Tx power is reduce by 1dB from default value
2)cold boot time combo cal is disabled.

Reference mail subject: 264031504(PMIC Fault/OCP Issue seen):
