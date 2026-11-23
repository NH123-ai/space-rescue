#include "space_rescue.h"

int read_target_data(RescueData* rd, const char* filename)
{
    if (rd == NULL || filename == NULL)
    {
        return 0;
    }
    FILE* fp = fopen(filename, "r");
    if (!fp)
    {
        return 0;
    }

    int n;
    if (fscanf(fp, "%d", &n) != 1)
    {
        fclose(fp);
        return 0;
    }
    if (n < 0 || n > MAX_TARGET_NUM)
    {
        fclose(fp);
        return 0;
    }
    rd->target_count = n;

    for (int i = 0; i < n; i++)
    {
        Target* t = &rd->target_list[i];
        int ret = fscanf(fp, "%lf %lf %lf %lf", &t->x, &t->y, &t->z, &t->life_max_time);
        if (ret != 4)
        {
            fclose(fp);
            return 0;
        }
        t->is_saved = 0;
        t->save_time = 0.0;
    }
    fclose(fp);
    return 1;
}

void print_target_info(RescueData* rd)
{
    if (rd == NULL)
        return;

    printf("\n===== 目标信息 =====\n");
    printf("目标数量：%d\n", rd->target_count);
    for (int i = 0; i < rd->target_count; i++)
    {
        Target* t = &rd->target_list[i];
        printf("目标[%2d] (%.2lf, %.2lf, %.2lf) 最长生存时间：%.2lf，已救援：%d\n",
            i, t->x, t->y, t->z, t->life_max_time, t->is_saved);
    }
    printf("====================\n\n");
}

double calc_3d_distance(double x1, double y1, double z1, double x2, double y2, double z2)
{
    double dx = x1 - x2;
    double dy = y1 - y2;
    double dz = z1 - z2;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

void update_target_pos(Target* tg, double dt)
{
    if (tg == NULL || dt <= 0)
        return;

    static int first = 1;
    if (first)
    {
        srand((unsigned)time(NULL));
        first = 0;
    }

    double offset = 0.08 * dt;
    double rx = ((double)rand() / RAND_MAX - 0.5) * offset;
    double ry = ((double)rand() / RAND_MAX - 0.5) * offset;
    double rz = ((double)rand() / RAND_MAX - 0.5) * offset;

    tg->x += rx;
    tg->y += ry;
    tg->z += rz;
}
