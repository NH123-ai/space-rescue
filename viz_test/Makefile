# ============================================================================
#  视觉模块独立测试 —— Makefile
#      make          只含 viz_module.c + mock_data.c（隔离调试）
#      make full     连真实 data_module.c / algorithm_module.c
#      make run      编译并跑全部测试
#      make demo     编译并进入交互沙箱
#      make emit     生成 out/trace.js 供 preview.html 预览
#      make clean    清理
# ============================================================================

CC      ?= gcc
CFLAGS  ?= -std=c99 -O1 -Wall -Wextra -I..
VIZ      = ../viz_module.c
BASE     = viz_test.c mock_data.c $(VIZ)

.PHONY: all full run demo emit clean

all: viz_test.exe

viz_test.exe: $(BASE) viz_assert.h mock_data.h
	$(CC) $(CFLAGS) -o $@ $(BASE)

full: viz_test_full.exe

viz_test_full.exe: $(BASE) ../data_module.c ../algorithm_module.c
	$(CC) $(CFLAGS) -DMOCK_WITH_REAL_MODULES -o $@ $(BASE) \
	      ../data_module.c ../algorithm_module.c

run: viz_test.exe
	./viz_test.exe

demo: viz_test.exe
	./viz_test.exe --demo

emit: viz_test.exe
	./viz_test.exe --emit out

clean:
	rm -f viz_test.exe viz_test_full.exe
	rm -rf out
