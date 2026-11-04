#pragma once
/* Latest complete sport snapshot wins; accepted data is owned by the worker. */
int openvela_ui_storage_submit(void *data,int (*write_snapshot)(void *));
int openvela_ui_storage_stop(void);
