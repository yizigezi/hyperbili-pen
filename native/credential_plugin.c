#include "quickjs.h"
#include "credential_store.h"
#include "credential_plugin.h"
extern void registerCModuleLoader(const char*, JSModuleDef* (*)(JSContext*, const char*));
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VAULT_PATH "/userdisk/hyperbili-pen.credentials"
#define TASK_LIMIT 4
typedef struct task {
  uint64_t id; char op[8]; char *password,*data,*result;
  int done,ok; char error[128];
  struct task *all_next,*queue_next;
} task;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static task *queue_head,*queue_tail,*all_tasks;
static unsigned outstanding; static uint64_t next_id=1;
static int started,stopping;

static void wipe_free(char *p){if(p){volatile char *v=p;size_t n=strlen(p);while(n--)*v++=0;free(p);}}
static void free_task(task*t){if(!t)return;wipe_free(t->password);wipe_free(t->data);wipe_free(t->result);free(t);}
static void *worker_main(void *unused){
  (void)unused;
  for(;;){task*t;
    pthread_mutex_lock(&lock);
    while(!queue_head&&!stopping)pthread_cond_wait(&ready,&lock);
    if(!queue_head&&stopping){pthread_mutex_unlock(&lock);break;}
    t=queue_head;queue_head=t->queue_next;if(!queue_head)queue_tail=NULL;t->queue_next=NULL;
    pthread_mutex_unlock(&lock);
    if(!strcmp(t->op,"store")){
      int rc=cs_store(VAULT_PATH,t->password,(const unsigned char*)t->data,strlen(t->data));
      t->ok=(rc==0);if(rc)snprintf(t->error,sizeof(t->error),"credential store failed (%d)",rc);
    }else if(!strcmp(t->op,"load")){
      unsigned char*out=NULL;size_t len=0;int rc=cs_load(VAULT_PATH,t->password,&out,&len);
      t->ok=(rc==0);
      if(rc)snprintf(t->error,sizeof(t->error),rc==-3?"vault password incorrect or data corrupted (-3)":"credential load failed (%d)",rc);
      else{t->result=malloc(len+1);if(t->result){memcpy(t->result,out,len);t->result[len]=0;}else{t->ok=0;snprintf(t->error,sizeof(t->error),"out of memory");}cs_free(out,len);}
    }else if(!strcmp(t->op,"delete")){
      int rc=cs_delete(VAULT_PATH);t->ok=(rc==0);if(rc)snprintf(t->error,sizeof(t->error),"credential delete failed (%d)",rc);
    }else{t->ok=0;snprintf(t->error,sizeof(t->error),"unknown operation");}
    pthread_mutex_lock(&lock);t->done=1;pthread_mutex_unlock(&lock);
  }return NULL;
}
static int ensure_worker(void){int rc=0;pthread_mutex_lock(&lock);if(!started){stopping=0;rc=pthread_create(&worker,NULL,worker_main,NULL);if(!rc)started=1;}pthread_mutex_unlock(&lock);return rc;}
static JSValue js_submit(JSContext*ctx,JSValueConst this_val,int argc,JSValueConst*argv){
  const char*op,*pw=NULL,*data=NULL;task*t;JSValue ret;(void)this_val;
  if(argc<2||!JS_IsString(argv[0])||!JS_IsString(argv[1]))return JS_ThrowTypeError(ctx,"hyperCredential.submit(op,password,data?)");
  op=JS_ToCString(ctx,argv[0]);pw=JS_ToCString(ctx,argv[1]);
  if(argc>2&&!JS_IsNull(argv[2])&&!JS_IsUndefined(argv[2]))data=JS_ToCString(ctx,argv[2]);
  if(!op||!pw||(!strcmp(op,"store")&&!data)){if(op)JS_FreeCString(ctx,op);if(pw)JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);return JS_ThrowTypeError(ctx,"invalid credential arguments");}
  if(strcmp(op,"store")&&strcmp(op,"load")&&strcmp(op,"delete")){JS_FreeCString(ctx,op);JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);return JS_ThrowTypeError(ctx,"unsupported credential operation");}
  if(strcmp(op,"delete")&&strlen(pw)<8){JS_FreeCString(ctx,op);JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);return JS_ThrowRangeError(ctx,"vault password must be at least 8 characters");}
  if(ensure_worker()){JS_FreeCString(ctx,op);JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);return JS_ThrowInternalError(ctx,"credential worker unavailable");}
  t=calloc(1,sizeof(*t));if(!t){JS_FreeCString(ctx,op);JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);return JS_ThrowOutOfMemory(ctx);}
  snprintf(t->op,sizeof(t->op),"%s",op);t->password=strdup(pw);if(data)t->data=strdup(data);
  JS_FreeCString(ctx,op);JS_FreeCString(ctx,pw);if(data)JS_FreeCString(ctx,data);
  if(!t->password||(!strcmp(t->op,"store")&&!t->data)){free_task(t);return JS_ThrowOutOfMemory(ctx);}
  pthread_mutex_lock(&lock);
  if(outstanding>=TASK_LIMIT||stopping){pthread_mutex_unlock(&lock);free_task(t);return JS_ThrowInternalError(ctx,"credential queue full");}
  t->id=next_id++;if(!next_id)next_id=1;t->all_next=all_tasks;all_tasks=t;outstanding++;
  if(queue_tail)queue_tail->queue_next=t;else queue_head=t;queue_tail=t;
  pthread_cond_signal(&ready);pthread_mutex_unlock(&lock);
  ret=JS_NewInt64(ctx,(int64_t)t->id);return ret;
}
static JSValue js_poll(JSContext*ctx,JSValueConst this_val,int argc,JSValueConst*argv){
  int64_t id;task**p,*t;JSValue obj;(void)this_val;
  if(argc<1||JS_ToInt64(ctx,&id,argv[0])<0||id<=0)return JS_ThrowTypeError(ctx,"hyperCredential.poll(id)");
  pthread_mutex_lock(&lock);for(p=&all_tasks;*p&&(*p)->id!=(uint64_t)id;p=&(*p)->all_next){}
  t=*p;if(!t){pthread_mutex_unlock(&lock);return JS_ThrowInternalError(ctx,"credential task not found");}
  if(!t->done){pthread_mutex_unlock(&lock);return JS_NULL;}
  *p=t->all_next;if(outstanding)outstanding--;pthread_mutex_unlock(&lock);
  obj=JS_NewObject(ctx);JS_SetPropertyStr(ctx,obj,"ok",JS_NewBool(ctx,t->ok));
  JS_SetPropertyStr(ctx,obj,"error",JS_NewString(ctx,t->error));
  if(t->result)JS_SetPropertyStr(ctx,obj,"data",JS_NewString(ctx,t->result));
  free_task(t);return obj;
}
static int module_init(JSContext*ctx,JSModuleDef*m){
  JSValue api=JS_NewObject(ctx);if(JS_IsException(api))return -1;
  JS_SetPropertyStr(ctx,api,"submit",JS_NewCFunction(ctx,js_submit,"submit",3));
  JS_SetPropertyStr(ctx,api,"poll",JS_NewCFunction(ctx,js_poll,"poll",1));
  JS_SetModuleExport(ctx,m,"default",api);return 0;
}
static JSModuleDef*module_load(JSContext*ctx,const char*name){
  JSModuleDef*m=JS_NewCModule(ctx,name,module_init);if(!m)return NULL;
  if(JS_AddModuleExport(ctx,m,"default")<0)return NULL;return m;
}
void register_hyper_credential_jsapi(void){registerCModuleLoader("hyperCredential",module_load);}

__attribute__((destructor)) static void credential_worker_shutdown(void) {
  task *t, *next;
  pthread_mutex_lock(&lock);
  if (!started) { pthread_mutex_unlock(&lock); return; }
  stopping = 1;
  pthread_cond_broadcast(&ready);
  pthread_mutex_unlock(&lock);
  pthread_join(worker, NULL);
  pthread_mutex_lock(&lock);
  t = all_tasks; all_tasks = NULL; queue_head = queue_tail = NULL;
  outstanding = 0; started = 0; stopping = 0;
  pthread_mutex_unlock(&lock);
  while (t) { next = t->all_next; free_task(t); t = next; }
}
