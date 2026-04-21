#ifndef FINAL_GRADUATE_WORK_PID_CONFIG_H
#define FINAL_GRADUATE_WORK_PID_CONFIG_H

/*
 * Shared default PID parameters.
 * These values only provide a compilable and safe baseline.
 * Real tuning should be done in the control task after hardware verification.
 */

#ifndef MOTOR_TARGET_SPEED
#define MOTOR_TARGET_SPEED (0.0f)
#endif

#ifndef MOTOR_SPEED_KP
#define MOTOR_SPEED_KP (1.0f)
#endif

#ifndef MOTOR_SPEED_KI
#define MOTOR_SPEED_KI (0.0f)
#endif

#ifndef MOTOR_SPEED_KD
#define MOTOR_SPEED_KD (0.0f)
#endif

#ifndef MOTOR_SPEED_OUTPUT_MAX
#define MOTOR_SPEED_OUTPUT_MAX (1000.0f)
#endif

#ifndef MOTOR_SPEED_INTEGRAL_MAX
#define MOTOR_SPEED_INTEGRAL_MAX (400.0f)
#endif

#ifndef MOTOR_POSITION_KP
#define MOTOR_POSITION_KP (1.0f)
#endif

#ifndef MOTOR_POSITION_KI
#define MOTOR_POSITION_KI (0.0f)
#endif

#ifndef MOTOR_POSITION_KD
#define MOTOR_POSITION_KD (0.0f)
#endif

#ifndef MOTOR_POSITION_OUTPUT_MAX
#define MOTOR_POSITION_OUTPUT_MAX (500.0f)
#endif

#ifndef MOTOR_POSITION_INTEGRAL_MAX
#define MOTOR_POSITION_INTEGRAL_MAX (200.0f)
#endif

#ifndef MOTOR_INNER_TARGET_MAX
#define MOTOR_INNER_TARGET_MAX (1000.0f)
#endif

#endif /* FINAL_GRADUATE_WORK_PID_CONFIG_H */
