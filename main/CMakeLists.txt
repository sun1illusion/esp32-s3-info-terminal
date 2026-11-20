idf_component_register(SRC_DIRS "."
                                "Board"
                                "Network"
                                "UI"
                       INCLUDE_DIRS "."
                                    "Board"
                                    "Network"
                                    "UI")

# 中文字体文件（myfont_16.c）默认 include "lvgl/lvgl.h"，
# 定义此宏让它改为 include "lvgl.h"，与 ESP-IDF 的 lvgl 组件头文件路径匹配
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
