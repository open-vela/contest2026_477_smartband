"""Build an actual-source TLS reader harness; no real credentials or network."""
import argparse, pathlib, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',required=True,type=pathlib.Path)
parser.add_argument('--baseline-source',type=pathlib.Path)
parser.add_argument('--output-dir',required=True,type=pathlib.Path)
parser.add_argument('--cc',default='gcc')
args=parser.parse_args()
out=args.output_dir
out.mkdir(parents=True,exist_ok=True)
after=args.source.read_text(encoding='utf-8')
versions=[]
if args.baseline_source:
    versions.append(('before',args.baseline_source.read_text(encoding='utf-8')))
versions.append(('after',after))
prefix=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#define CONN_POOL_SIZE 2
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY -0x7880
#define MBEDTLS_ERR_SSL_WANT_READ -0x6900
#define VELA_TLS_ERR_READ -7
static const char *TAG="test";
typedef struct { int ssl; } tls_ctx_t;
static const char *wire;
static size_t wire_size, wire_pos, fragment;
static int reads_past_message;
static int mbedtls_ssl_read(void *ctx, unsigned char *buf, size_t cap) {
    (void)ctx;
    if(wire_pos==wire_size) { reads_past_message++; return -0x50; }
    size_t n=wire_size-wire_pos;
    if(n>fragment)n=fragment;
    if(n>cap)n=cap;
    memcpy(buf,wire+wire_pos,n);wire_pos+=n;return (int)n;
}
'''
suffix=r'''
static void run_case(const char *body, const char *decoded, size_t frag,
                     bool valid, size_t cap, const char *extra_header) {
    char input[40000], output[40000];
    snprintf(input,sizeof(input),"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n%s\r\n%s",extra_header,body);
    wire=input;wire_size=strlen(input);wire_pos=0;fragment=frag;reads_past_message=0;
    tls_ctx_t ctx={0};size_t body_len=0;bool keep=false;
    int result=tls_read_response(&ctx,output,cap,&body_len,&keep);
    if(valid) {
        if(result!=200 || strcmp(output,decoded)!=0 || body_len!=strlen(decoded) || reads_past_message!=0) {
            fprintf(stderr,"FAIL result=%d extra_reads=%d fragment=%zu body_len=%zu\n",result,reads_past_message,frag,body_len);
            exit(1);
        }
        assert(keep);
    } else {
        assert(result==VELA_TLS_ERR_READ);
        assert(!keep);
    }
}
int main(void) {
    size_t sizes[]={1,2,7,64,4096};
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        run_case("5\r\nhello\r\n0\r\n\r\n","hello",sizes[i],true,40000,"");
        run_case("5;name=value\r\nhello\r\n0\r\nX-Test: yes\r\n\r\n","hello",sizes[i],true,40000,"");
        run_case("5\r\n0\r\n\r\n\r\n0\r\n\r\n","0\r\n\r\n",sizes[i],true,40000,"");
        run_case("0\r\n\r\n","",sizes[i],true,40000,"");
        run_case("5\r\nhello\r\n0\r\n\r\n","hello",sizes[i],true,40000,"Content-Length: 1\r\n");
    }
    run_case("5\r\nhello\r\n0\r\n", "",7,false,40000,"");
    run_case("5\r\nhel", "",7,false,40000,"");
    run_case("5\r\nhelloXX0\r\n\r\n", "",7,false,40000,"");
    run_case("-1\r\nx\r\n0\r\n\r\n", "",7,false,40000,"");
    run_case("FFFFFFFFFFFFFFFFFFFFFFFF\r\n", "",7,false,40000,"");
    run_case("5\r\nhello\r\n0\r\n\r\n", "",7,false,8,"");
    char large[11000], chunks[12000];
    memset(large,'x',10000);large[10000]=0;
    snprintf(chunks,sizeof(chunks),"2710\r\n%s\r\n0\r\n\r\n",large);
    run_case(chunks,large,777,true,40000,"");
    puts("PASS 32 actual TLS reader cases; complete chunks never read for connection close");
    return 0;
}
'''
for name,src in versions:
    helpers=src[src.index('/* ── Chunked transfer decoding'):src.index('/* ── TLS context')]
    reader=src[src.index('#define TLS_RAW_BUF_SIZE'):src.index('/* ── Public API')]
    source=out/(name+'.c')
    source.write_text(prefix+helpers+reader+suffix,encoding='utf-8')
    binary=out/name
    subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Werror','-pthread',
                    '-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary.resolve())])
    if name=='before':
        if result.returncode!=1: raise SystemExit('Baseline did not reproduce the expected assertion')
        print('Baseline reproduced unwanted read after complete response',flush=True)
    elif result.returncode:
        raise SystemExit(result.returncode)
