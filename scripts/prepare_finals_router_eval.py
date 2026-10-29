"""Compile actual pinned rule dispatcher + repository C router; tools are spies.

Only extracts the NL fast path, so this does NOT test the earlier injection guard,
full agent context or live tool execution. Inputs contain no prompt-injection set.
"""
import argparse
import ast
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    target = args.output_dir / "agent"
    target.mkdir(exist_ok=True)
    files = ["CMakeLists.txt", "Makefile", "Kconfig", "src/tools/skill_loader.c",
             "src/tools/tool_registry.c", "src/core/agent_loop.c", "src/infra/vela_tls.c"]
    for file in files:
        dest = target / file
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(args.upstream / file, dest)
    subprocess.run(["git", "init", "-q", str(target)], check=True)
    for name in ["overlay", "http_chunked", "skill_route", "finals", "local_router"]:
        subprocess.run(["git", "apply", "--ignore-space-change", str(ROOT / "agent" / f"packages_ai_agent_{name}.patch")], cwd=target, check=True)
    tree = ast.parse((ROOT / "scripts/test_skill_context_route.py").read_text(encoding="utf-8"))
    prefix = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                  and any(isinstance(t, ast.Name) and t.id == "prefix" for t in node.targets))
    prefix = prefix[:prefix.index("static char *mooncat_local_router_handle(")]
    prefix = prefix.replace("static int tool_registry_execute", "int tool_registry_execute")
    prefix = prefix.replace("static char called_tool[64];", "static char called_tool[64], called_args[2048];")
    prefix = prefix.replace("(void)input; tool_calls++;", 'snprintf(called_args,sizeof(called_args),"%s",input); tool_calls++;')
    prefix = prefix.replace('snprintf(out,cap,"{}"); return 0;', 'snprintf(out,cap,"%s",!strcmp(name,"get_current_time")?"UNIX epoch: 1800000000":"{}"); return 0;')
    prefix += '''
#include "mooncat_local_router.h"
static struct mooncat_router_prediction_s supplied;
int mooncat_intent_model_predict(const char *text,struct mooncat_router_prediction_s *p) {
    (void)text;router_calls++;*p=supplied;return 0;
}
'''
    source = (target / "src/core/agent_loop.c").read_text(encoding="utf-8")
    start = source.index("static bool contains_any(")
    end = source.index("/* ── Handle slash commands", start)
    suffix = r'''
int main(void) {
    char hex[8192],text[4096];int intent;float confidence,margin;
    while(scanf("%d %f %f %8191s",&intent,&confidence,&margin,hex)==4) {
        size_t len=strlen(hex);if(len%2 || len/2>=sizeof(text))return 2;
        for(size_t i=0;i<len;i+=2) {unsigned b;if(sscanf(hex+i,"%2x",&b)!=1)return 2;text[i/2]=(char)b;}
        text[len/2]=0;supplied.intent=intent;supplied.confidence=confidence;supplied.margin=margin;
        tool_calls=router_calls=0;called_tool[0]=called_args[0]=0;
        char *reply=handle_nl_fast_path(text);
        const char *route=router_calls?(intent==4?"fallback":tool_calls?"model":"parameter_rejected"):(reply?"rule":"context_fallback");
        if(reply && strstr(reply,"请求包含否定或条件"))route="request_rejected";
        printf("%s\t%s\t%s\n",route,called_tool,called_args);free(reply);
    }
    return 0;
}
'''
    code = args.output_dir / "route_eval.c"
    code.write_text(prefix + source[start:end] + suffix, encoding="utf-8")
    binary = args.output_dir / "route_eval"
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "agent"),
                    "-I", str(ROOT / "tests/host/include"), str(code),
                    str(ROOT / "agent/mooncat_local_router.c"), "-o", str(binary)], check=True)
    print(binary.resolve())


if __name__ == "__main__":
    main()
