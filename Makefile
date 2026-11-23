# ============================================================================
#  太空救援问题 —— Makefile（gcc / MinGW-w64 或 Linux glibc 均可用）
#  常用目标：
#     make            构建 space_rescue.exe
#     make run        运行并生成 web/trace.js
#     make test       运行内置测试用例
#     make trace      仅重新生成 web 轨迹
#     make clean      清理产物
#  可选：
#     make easyx      EasyX 图形版（需 -DEASYX 及 EasyX 库，仅 Windows）
# ============================================================================

CC      ?= gcc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -I. -Iteammate
LDLIBS  ?= -lm
SRCS     = main.c teammate/data_module_a.c teammate/algorithm_module_b.c teammate/plan_adapter.c viz_module.c
OBJS     = $(SRCS:.c=.o)
TARGET   = space_rescue

ifeq ($(OS),Windows_NT)
TARGET  := space_rescue.exe
endif

.PHONY: all run test trace clean easyx

all: $(TARGET)

$(TARGET): $(SRCS) space_rescue.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET) --trace web

trace: $(TARGET)
	./$(TARGET) --trace web

test: $(TARGET)
	./$(TARGET) --test

easyx: $(SRCS) viz_easyx_optional.c space_rescue.h
	$(CC) $(CFLAGS) -DEASYX -o space_rescue_easyx $(SRCS) viz_easyx_optional.c \
	      $(LDLIBS) -lgraphics -lgdi32 -lwinmm -lole32 -luuid

clean:
	rm -f $(OBJS) space_rescue space_rescue.exe space_rescue_easyx space_rescue_easyx.exe
	rm -f _run_stdout.txt _run_test.txt
