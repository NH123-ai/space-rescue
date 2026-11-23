/*============================================================================
 *  rescue_internal.h  --  队友代码与适配层之间的内部工具（不是对外接口）
 *----------------------------------------------------------------------------
 *  背景：题目里目标位置是“低速漂移”的，所以“飞到目标需要多久”不能简单用
 *  距离/速度 —— 因为飞行期间目标自己也在动。准确做法是解一个一元二次方程
 *  （拦截时间的解析解）。这个工具原来散落在各自的实现里，集成时抽到一处，
 *  让队友 A（数据）、队友 B（算法）与适配层用同一套公式，避免三方对不齐。
 *
 *  这些函数**不在** space_rescue.h 的对外契约里，只供工程内部使用。
 *===========================================================================*/
#ifndef RESCUE_INTERNAL_H
#define RESCUE_INTERNAL_H

#include "space_rescue.h"

/* 目标在时刻 t 的位置：p(t) = p0 + v·t（A 的漂移模型的解析形式） */
void   rescue_pos_at(const Target *tg, double t, double *px, double *py, double *pz);

/* 从 from 点、于 t_begin 时刻出发，拦截（追上）目标所需的飞行时间
 *   |p0 + v·(t_begin+τ) − from| = SPEED · τ
 * 展开成 (|v|²−SPEED²)τ² + 2(D·v)τ + |D|² = 0，取最小非负根 */
double rescue_intercept_time(const Target *tg, double t_begin,
                             double from_x, double from_y, double from_z);

#endif /* RESCUE_INTERNAL_H */
