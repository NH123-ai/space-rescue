/*============================================================================
 *  viz_easyx_optional.c  --  组员 C：EasyX 原生动画（可选实现）
 *----------------------------------------------------------------------------
 *  说明：
 *    · 本文件只在编译时定义 EASYX 宏才会产生代码：
 *          gcc -DEASYX ... viz_easyx_optional.c -lgraphics -lgdi32 ...
 *      或直接用 VS2022 + EasyX 安装包（推荐图形化课程环境）。
 *    · 它实现 space_rescue.h 中 #ifdef EASYX 段声明的三个函数，
 *      与 Web 版共享同一份 RescueData，属于“同一数据、两种渲染器”。
 *    · 课堂演示建议用 Web 版（更清晰、可拖拽时间轴）；如果教师要求
 *      “关键知识点：EasyX / QT / VS 对话框”，本文件即可满足该要求。
 *    · 注意：本文件未在无 EasyX 环境下编译验证，若你的 EasyX 版本 API
 *      有差异（如 fillcircle 改名），按提示替换即可，逻辑与 Web 版一致。
 *===========================================================================*/
#include "space_rescue.h"

#ifdef EASYX

#include <graphics.h>
#include <conio.h>
#include <tchar.h>

#define WIN_W 1024
#define WIN_H 640

/* 三维 -> 二维的简易等轴测投影：
 *   sx = cx + (x - z) * cos30 * s
 *   sy = cy + (x + z) * sin30 * s - y * s
 * 只做俯视偏斜的示意投影，足够表达“空间位置 + 相对距离”。 */
static void project_iso(double x, double y, double z, double scale,
                        int *sx, int *sy)
{
    double c30 = 0.8660254, s30 = 0.5;
    *sx = (int)(WIN_W * 0.5 + (x - z) * c30 * scale);
    *sy = (int)(WIN_H * 0.55 + ((x + z) * s30 - y) * scale);
}

/* 目标在时刻 t 的位置（线性漂移） */
static void tpos(const Target *tg, double t, double *x, double *y, double *z)
{
    target_pos_at(tg, t, x, y, z);
}

/* 飞船时刻 t 的位置：沿 best_seq 的航段线性插值 */
static int ship_pos(const RescueData *rd, double t, double *x, double *y, double *z)
{
    int    i;
    double t_prev = 0.0;
    double px = rd->origin_x, py = rd->origin_y, pz = rd->origin_z;

    for (i = 0; i < MAX_TARGET_NUM; ++i) {
        int idx = rd->best_seq[i];
        double t_end, ex, ey, ez, k;

        if (idx < 0 || idx >= rd->target_count) break;
        if (!rd->target_list[idx].is_saved) continue;

        t_end = rd->target_list[idx].save_time;
        tpos(&rd->target_list[idx], t_end, &ex, &ey, &ez);

        if (t <= t_end) {
            k = (t_end > t_prev) ? (t - t_prev) / (t_end - t_prev) : 1.0;
            if (k < 0.0) k = 0.0;
            if (k > 1.0) k = 1.0;
            *x = px + (ex - px) * k;
            *y = py + (ey - py) * k;
            *z = pz + (ez - pz) * k;
            return 1;
        }
        t_prev = t_end;
        px = ex; py = ey; pz = ez;
    }

    /* 任务结束：停在最后一个目标 */
    *x = px; *y = py; *z = pz;
    (void)i;
    return 0;
}

void init_easyx_window(void)
{
    initgraph(WIN_W, WIN_H);
    setbkcolor(RGB(6, 10, 18));
    cleardevice();
    BeginBatchDraw();
}

void draw_3d_projection(RescueData *rd, double now_time)
{
    int    i;
    double scale = 2.0;       /* 世界单位 -> 像素 */
    int    sx, sy;
    TCHAR  buf[64];
    double x, y, z;

    cleardevice();

    /* 标题与时钟 */
    settextcolor(RGB(120, 220, 255));
    settextstyle(18, 0, _T("Consolas"));
    _stprintf_s(buf, 64, _T("SPACE RESCUE   T+%.2f / %.2f"), now_time, rd->total_time);
    outtextxy(18, 14, buf);

    /* 原点（救援基地） */
    project_iso(rd->origin_x, rd->origin_y, rd->origin_z, scale, &sx, &sy);
    setfillcolor(RGB(40, 200, 240));
    solidcircle(sx, sy, 7);
    settextcolor(RGB(150, 210, 240));
    outtextxy(sx + 10, sy - 6, _T("BASE"));

    /* 目标 */
    for (i = 0; i < rd->target_count; ++i) {
        const Target *tg = &rd->target_list[i];
        int    saved = tg->is_saved && now_time >= tg->save_time - 1e-9;
        int    dead  = !tg->is_saved && now_time >= tg->life_max_time;
        COLORREF col = saved ? RGB(60, 225, 160)
                     : dead  ? RGB(255, 92, 114)
                             : RGB(90, 200, 235);

        tpos(tg, now_time, &x, &y, &z);
        project_iso(x, y, z, scale, &sx, &sy);

        setlinecolor(col);
        setfillcolor(col);
        circle(sx, sy, 8);
        solidcircle(sx, sy, 3);

        /* 剩余生存时间条 */
        {
            double ratio = (tg->life_max_time > 0.0)
                         ? (tg->life_max_time - now_time) / tg->life_max_time : 0.0;
            int w;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            w = (int)(ratio * 40.0);
            setlinecolor(RGB(60, 80, 100));
            rectangle(sx - 20, sy + 12, sx + 20, sy + 16);
            setfillcolor(ratio > 0.35 ? RGB(60, 200, 235) : RGB(255, 120, 90));
            solidrectangle(sx - 20, sy + 12, sx - 20 + w, sy + 16);
        }

        _stprintf_s(buf, 64, _T("T%d"), i + 1);
        outtextxy(sx - 6, sy - 24, buf);
    }

    /* 飞船 */
    if (ship_pos(rd, now_time, &x, &y, &z)) {
        project_iso(x, y, z, scale, &sx, &sy);
        setfillcolor(RGB(240, 255, 255));
        setlinecolor(RGB(53, 214, 245));
        {
            POINT tri[3];
            tri[0].x = sx + 9;  tri[0].y = sy;
            tri[1].x = sx - 6;  tri[1].y = sy + 5;
            tri[2].x = sx - 6;  tri[2].y = sy - 5;
            fillpolygon(tri, 3);
        }
    }

    /* 底部信息 */
    settextcolor(RGB(150, 175, 200));
    _stprintf_s(buf, 64, _T("目标 %d  已获救 %d  牺牲 %d"),
                rd->target_count, rd->saved_count, rd->dead_count);
    outtextxy(18, WIN_H - 34, buf);
}

void run_rescue_animation(RescueData *rd)
{
    double t;
    double step = rd->total_time / 240.0;

    if (step <= 0.0) step = 0.1;
    if (step > 0.5) step = 0.5;

    init_easyx_window();
    for (t = 0.0; t <= rd->total_time + 1e-9; t += step) {
        draw_3d_projection(rd, t);
        FlushBatchDraw();
        Sleep(20);
        if (_kbhit()) { _getch(); break; }
    }
    draw_3d_projection(rd, rd->total_time);
    FlushBatchDraw();
    _getch();
    EndBatchDraw();
    closegraph();
}

#endif /* EASYX */
