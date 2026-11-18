/*============================================================================
 *  mock_data.h  --  视觉模块独立调试用的假数据 / 造数据工具声明
 *===========================================================================*/
#ifndef MOCK_DATA_H
#define MOCK_DATA_H

#include "space_rescue.h"

/* ---- 造场景 ---- */
void scene_reset(RescueData *rd, int scenario, unsigned seed);
int  scene_add(RescueData *rd, double x, double y, double z, double life);
int  scene_add_vel(RescueData *rd, double x, double y, double z,
                   double vx, double vy, double vz, double life);
void scene_ring(RescueData *rd, int n, double dist, double life, double life_step);

/* ---- 造方案 ---- */
double mock_intercept_time(const Target *tg, double t_begin,
                           double fx, double fy, double fz);
void   mock_build_plan(RescueData *rd, const int order[], int n,
                       int algo, RoutePlan *out);
void   mock_nearest_plan(RescueData *rd, RoutePlan *out);
void   mock_urgency_plan(RescueData *rd, RoutePlan *out);

#endif /* MOCK_DATA_H */
