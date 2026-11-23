#include "space_rescue.h"

/*
函数：check_time_constraint
功能：模块B要求：校验一个救援下标序列seq，按顺序模拟飞船飞行，判断全部目标是否都不超过各自life_max_time
参数：
    rd:救援数据结构体
    seq[]:存放目标下标的数组，比如{0,2,1}代表先救0号，再2号，再1号
    len:序列有效长度，有多少个目标要救援
返回：1=全部时间约束满足，可以成功救援；0=任意一个目标超时，该序列不可行
*/
int check_time_constraint(RescueData* rd, int seq[], int len)
{
    // 判空保护，健壮性，大一写代码要做指针判空
    if (rd == NULL || seq == NULL)
    {
        return 0;
    }

    double ship_x = 0.0;   //飞船初始位置：原点(0,0,0)，题目规定救援队在原点
    double ship_y = 0.0;
    double ship_z = 0.0;
    double current_time = 0.0; //当前任务时钟，初始时间0

    //循环遍历这个救援序列里每一个目标
    for (int i = 0; i < len; i++)
    {
        int target_idx = seq[i]; //拿到这一步要救的目标在target_list的下标

        //下标合法性检查，防止数组越界
        if (target_idx < 0 || target_idx >= rd->target_count)
        {
            return 0;
        }

        Target* t = &rd->target_list[target_idx];

        //调用A模块提供的calc_3d_distance计算飞船到目标的距离，模块B依赖A模块接口
        double dist = calc_3d_distance(ship_x, ship_y, ship_z, t->x, t->y, t->z);
        double fly_time = dist / SPEED; //飞行时间 = 距离 / 飞船速度SPEED（头文件宏定义）

        current_time = current_time + fly_time; //到达这个目标时刻

        //核心约束判断：到达时间 >目标最长生存时间 →救援失败，这个序列不行
        if (current_time > t->life_max_time)
        {
            return 0;
        }

        //更新飞船位置：救完这个目标，飞船就在该目标坐标，下一段从这里起飞
        ship_x = t->x;
        ship_y = t->y;
        ship_z = t->z;
    }
    //全部目标走完没有超时，序列合法
    return 1;
}


/*
函数：tsp_greedy
模块B需求：贪心算法求解救援顺序：每次从当前飞船位置，选距离最近、还没选过的目标，生成救援序列存入rd->best_seq
结果还要写回每个target的save_time，设置rd->find_success标记
注意：贪心不一定全局最优，只是局部每次选最近，TSP最近邻贪心
*/
void tsp_greedy(RescueData* rd)
{
    //指针判空，防止传空指针程序崩溃
    if (rd == NULL)
    {
        return;
    }
    int n = rd->target_count;
    if (n <= 0)
    {
        rd->find_success = 0;
        return;
    }

    //visited数组标记目标是否已经被选入救援序列，0未选，1已选
    int visited[MAX_TARGET_NUM] = { 0 };
    int seq_len = 0; //当前best_seq里面存了多少个目标

    //飞船初始位置原点
    double ship_x = 0.0;
    double ship_y = 0.0;
    double ship_z = 0.0;
    double now_time = 0.0;

    //循环n轮，每一轮挑选一个目标加入救援序列，一共n个目标
    for (int round = 0; round < n; round++)
    {
        int select_idx = -1; //本轮选中的目标下标，-1代表本轮找不到可选目标
        double min_dist = 1e18; //初始一个很大距离，用来找最近目标

        //遍历全部目标，筛选：没有被选过的目标
        for (int i = 0; i < n; i++)
        {
            if (visited[i] == 1)
            {
                continue; //这个目标已经进序列，跳过
            }
            //调用A模块距离计算函数
            double d = calc_3d_distance(ship_x, ship_y, ship_z,
                rd->target_list[i].x, rd->target_list[i].y, rd->target_list[i].z);

            //如果这个目标距离比当前最小距离更小，更新选择
            if (d < min_dist)
            {
                min_dist = d;
                select_idx = i;
            }
        }

        //如果select_idx=-1 说明没有可以选的目标，任务无解
        if (select_idx == -1)
        {
            break;
        }

        //把选中目标加入best_seq，这是C模块要读取的结果字段！接口约定
        rd->best_seq[seq_len] = select_idx;
        seq_len++;
        visited[select_idx] = 1; //标记已经选择

        Target* tg = &rd->target_list[select_idx];
        //飞行时间
        double fly_t = min_dist / SPEED;
        now_time = now_time + fly_t;

        //把救援到达时刻写入结构体，给D模块print_result输出、C模块模拟使用
        tg->save_time = now_time;

        //更新飞船位置到刚刚救援完的目标坐标
        ship_x = tg->x;
        ship_y = tg->y;
        ship_z = tg->z;
    }

    //全部n个目标都选到序列里面，并且校验时间约束是否全部满足
    if (seq_len == n && check_time_constraint(rd, rd->best_seq, n) == 1)
    {
        rd->find_success = 1; //找到可行方案
    }
    else
    {
        rd->find_success = 0; //贪心找不到可行方案
    }
    rd->total_time = now_time; //记录总救援耗时，头文件RescueData成员
}


// ====================== 回溯算法 tsp_backtrack =====================
// 回溯用到局部辅助变量，写在函数内部，不使用全局变量；
// 思路：生成全部排列，调用check_time_constraint判断序列合法；记录总飞行距离最小的那一组序列，存到rd->best_seq；
// 因为是大一水平，不用剪枝优化，逻辑直白，缺点n=12会很慢，作业测试建议n<=9。

/*
回溯内部递归辅助函数
参数说明：
rd：原始救援数据
cur_seq[] 当前正在构建的排列序列
used[]标记哪些下标已经被放进cur_seq
cur_len 当前cur_seq已经放了多少目标
best_len 固定等于总目标数量n
*best_total_dist 保存当前找到最优序列的总飞行距离
temp_best[] 保存当前最优的序列副本，递归内部更新，最后复制给rd->best_seq
*/
static void backtrack_dfs(RescueData* rd, int cur_seq[], int used[], int cur_len, int best_len,
    double* best_total_dist, int temp_best[])
{
    int n = rd->target_count;
    //递归终止条件：当前序列长度等于全部目标数，得到一个完整排列
    if (cur_len == best_len)
    {
        //第一步：检查这个序列是否满足生存时间约束，调用模块B的check_time_constraint
        if (check_time_constraint(rd, cur_seq, cur_len) == 1)
        {
            //计算这个完整序列总飞行路程
            double sx = 0, sy = 0, sz = 0;
            double sum_dist = 0.0;
            for (int i = 0; i < cur_len; i++)
            {
                int idx = cur_seq[i];
                Target* t = &rd->target_list[idx];
                double d = calc_3d_distance(sx, sy, sz, t->x, t->y, t->z);
                sum_dist += d;
                sx = t->x;
                sy = t->y;
                sz = t->z;
            }
            //如果当前序列总距离比记录的最优距离更小 →更新最优解
            if (sum_dist < *best_total_dist)
            {
                *best_total_dist = sum_dist;
                //把当前cur_seq复制到temp_best临时数组保存最优排列
                for (int k = 0; k < n; k++)
                {
                    temp_best[k] = cur_seq[k];
                }
            }
        }
        return; //返回上一层递归
    }

    //循环所有目标，挑选还没有被使用的，加入当前序列继续递归
    for (int i = 0; i < n; i++)
    {
        if (used[i] == 1)
        {
            continue;
        }
        used[i] = 1;        //标记i号目标已经被使用
        cur_seq[cur_len] = i; //放入当前序列
        backtrack_dfs(rd, cur_seq, used, cur_len + 1, best_len, best_total_dist, temp_best);
        used[i] = 0; //回溯！撤销标记，尝试下一种排列
    }
}


/*
函数：tsp_backtrack
模块B需求：回溯枚举全部排列，寻找满足时间约束，总路程最短救援序列，结果写入rd->best_seq；设置find_success标记，填写每个target的save_time
*/
void tsp_backtrack(RescueData* rd)
{
    if (rd == NULL)
    {
        return;
    }
    int n = rd->target_count;
    if (n <= 0)
    {
        rd->find_success = 0;
        return;
    }

    int cur_seq[MAX_TARGET_NUM] = { 0 };   //递归中正在构造的序列
    int used[MAX_TARGET_NUM] = { 0 };      //标记是否选用
    int temp_best[MAX_TARGET_NUM] = { 0 }; //保存搜索得到最优序列临时副本

    //初始最优距离给一个极大值，任何合法解都会比它小
    double best_dist = 1e18;

    //调用递归回溯函数，遍历全部排列
    backtrack_dfs(rd, cur_seq, used, 0, n, &best_dist, temp_best);

    //判断是否找到可行解：best_dist被修改，不再等于极大值
    if (best_dist < 1e18)
    {
        rd->find_success = 1;
        //把临时最优序列复制到rd->best_seq，给C模块、D模块读取，这是跨模块关键输出！
        for (int i = 0; i < n; i++)
        {
            rd->best_seq[i] = temp_best[i];
        }

        //根据best_seq序列重新计算每个目标save_time，填充到target_list，供其他模块使用
        double ship_x = 0, ship_y = 0, ship_z = 0;
        double time_now = 0;
        for (int i = 0; i < n; i++)
        {
            int idx = rd->best_seq[i];
            Target* t = &rd->target_list[idx];
            double dist = calc_3d_distance(ship_x, ship_y, ship_z, t->x, t->y, t->z);
            double ft = dist / SPEED;
            time_now += ft;
            t->save_time = time_now;

            ship_x = t->x;
            ship_y = t->y;
            ship_z = t->z;
        }
        rd->total_time = time_now;
    }
    else
    {
        //所有排列全部不满足时间约束，无解
        rd->find_success = 0;
        rd->total_time = 0.0;
    }
}
