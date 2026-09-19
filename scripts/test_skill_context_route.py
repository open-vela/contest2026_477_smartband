"""Compile the actual NL fast path and check dispatch, with tools stubbed."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--baseline-source', type=Path)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
prefix = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <syslog.h>
#define CONFIG_AI_AGENT_LOCAL_TOOL_ROUTER 1
#define TAG "test"
static int s_fast_path_count, s_llm_path_count, tool_calls, router_calls;
static char called_tool[64];
typedef struct { char *valuestring; } cJSON;
static cJSON *cJSON_Parse(const char *s) { (void)s; return NULL; }
static cJSON *cJSON_GetObjectItem(cJSON *j,const char *s) {(void)j;(void)s;return NULL;}
static cJSON *cJSON_GetArrayItem(cJSON *j,int n) {(void)j;(void)n;return NULL;}
static bool cJSON_IsString(cJSON *j) {(void)j;return false;}
static void cJSON_Delete(cJSON *j) {(void)j;}
static int tool_registry_execute(const char *name,const char *input,char *out,size_t cap) {
    (void)input; tool_calls++; snprintf(called_tool,sizeof(called_tool),"%s",name);
    snprintf(out,cap,"{}"); return 0;
}
static size_t skill_loader_build_summary(char *out,size_t cap) {
    return (size_t)snprintf(out,cap,"SKILL_LIST");
}
static char *mooncat_local_router_handle(const char *text) {
    (void)text; router_calls++; return strdup("LOCAL_ROUTER");
}
'''
suffix = r'''
static void check(const char *text, const char *expected) {
    tool_calls=router_calls=0; called_tool[0]=0;
    char *reply=handle_nl_fast_path(text);
    bool ok;
    if(!strcmp(expected,"CLOUD")) ok=!reply && !tool_calls && !router_calls;
    else if(!strcmp(expected,"SKILL_LIST")) ok=reply && !strcmp(reply,expected) && !tool_calls && !router_calls;
    else if(!strcmp(expected,"LOCAL_ROUTER")) ok=reply && router_calls==1 && !tool_calls;
    else ok=reply && tool_calls==1 && !strcmp(called_tool,expected) && !router_calls;
    free(reply);
    if(!ok) { fprintf(stderr,"FAIL expected=%s tool=%s router_calls=%d input=%s\n",expected,called_tool,router_calls,text); exit(1); }
}
int main(void) {
    check("请读取 mooncat-active-coach Skill，并按其中说明用 mode=preview 分析一组明确标记为模拟的数据：valid=true、workout_active=false、do_not_disturb=false、inactivity_minutes=10、sleep_debt_minutes=150、stress_score=20、battery_percent=80。不要执行通知。", "CLOUD");
    check("Read the WEATHER skill and analyze my steps and battery", "CLOUD");
    check("按技能分析这份步数数据，不要通知", "CLOUD");
    check("preview battery_percent=80", "CLOUD");
    check("分析 inactivity_minutes=10", "CLOUD");
    check("分析 sleep_debt_minutes=150", "CLOUD");
    check("分析 stress_score=20", "CLOUD");
    check("分析 workout_active=false", "CLOUD");
    check("分析 do_not_disturb=true", "CLOUD");
    check("mooncat_coach_tick preview", "CLOUD");
    check("技能列表", "SKILL_LIST");
    check("有什么技能", "SKILL_LIST");
    check("LIST SKILL", "SKILL_LIST");
    check("list skills", "SKILL_LIST");
    check("现在几点了", "get_current_time");
    check("电池电量", "get_battery");
    check("battery", "get_battery");
    check("心率多少", "get_heartrate");
    check("今天走了多少步", "get_steps");
    check("暂停音乐", "music_pause");
    check("停止播放", "music_stop");
    check("继续播放", "music_resume");
    check("北京天气怎么样", "get_weather");
    check("20秒后主动提醒我休息", "LOCAL_ROUTER");
    check("先预览一条休息建议", "LOCAL_ROUTER");
    puts("PASS: 25 actual-source dispatch cases; supplied observations cause zero local tool/router calls");
}
'''
versions = [('after', args.source)]
if args.baseline_source:
    versions.insert(0, ('before', args.baseline_source))
for name, path in versions:
    source = path.read_text(encoding='utf-8')
    start = source.index('static bool contains_any(')
    end = source.index('/* ── Handle slash commands', start)
    code = args.output_dir / (name + '.c')
    binary = args.output_dir / name
    code.write_text(prefix + source[start:end] + suffix, encoding='utf-8')
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', str(code), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    print(name, 'exit=', result.returncode, flush=True)
    print(result.stdout + result.stderr, end='', flush=True)
    if name == 'before':
        assert result.returncode == 1 and 'expected=CLOUD tool=get_battery' in result.stderr
    else:
        assert result.returncode == 0
