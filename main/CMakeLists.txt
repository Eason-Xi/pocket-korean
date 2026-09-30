# 韩语学习应用。基线的 demo_*.c / ui_pixel*.c / demo_navigation.c 仍在本目录里
# （主机测试和参考用），但不参与固件构建：派生应用必须使用自己重新设计的界面。
idf_component_register(
    SRCS "main.c"
         "ko_app.c"
         "ko_audio.c"
         "ko_store.c"
         # 纯逻辑（主机测试覆盖，见 tests/test_ko_*.c）
         "ko_content.c"
         "ko_content_gen.c"
         "ko_crc32.c"
         "ko_progress.c"
         "ko_rng.c"
         "ko_session.c"
         "ko_quiz.c"
         "ko_input.c"
         "ko_power.c"
         "ko_nav.c"
         "ko_layout.c"
         "ko_adpcm.c"
         "ko_pack.c"
         "ko_tone.c"
         "ko_view.c"
         # 界面
         "ko_fonts.c"
         "ko_ui.c"
         "ko_ui_alpha.c"
         "ko_ui_card.c"
         "ko_ui_quiz.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_partition heap esp_system
)

# 裁剪后的 LVGL 字体：由 tools/gen_korean_assets.py 从 assets/korean/content.json 生成，
# 字符集与来源记录在 assets/fonts/ko_fonts.manifest.json。
file(GLOB KO_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ko_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${KO_FONT_SOURCES})

# 发音包（可选）：本地用 tools/korean_audio.py 生成 assets/audio/generated/kopack.bin 后，
# 它会作为 kopack 分区的镜像进入烧录清单，从而也进入合并固件（从 0x0 一次烧完）。
# 没有这个文件时固件照常构建，只是没有发音（应用会在设置页显示"发音包：未安装"）。
#
# 先复制进构建目录再登记：IDF 会把 flash_args 里的外部文件写成"相对构建目录"的路径，
# 而 macOS 上 /tmp 是指向 /private/tmp 的软链接，esptool 在真实路径里执行时会少算一级，
# 导致合并固件时找不到文件。放进构建目录后 flash_args 里就是 "kopack.bin"，
# 与 bootloader 等镜像一样。configure_file 也会在源文件变化时触发重新配置。
set(KO_PACK_IMAGE "${CMAKE_CURRENT_LIST_DIR}/../assets/audio/generated/kopack.bin")
if(EXISTS "${KO_PACK_IMAGE}")
    configure_file("${KO_PACK_IMAGE}" "${CMAKE_BINARY_DIR}/kopack.bin" COPYONLY)
    esptool_py_flash_to_partition(flash "kopack" "${CMAKE_BINARY_DIR}/kopack.bin")
    message(STATUS "Korean learner: pronunciation pack ${KO_PACK_IMAGE} will be flashed")
else()
    message(STATUS "Korean learner: no pronunciation pack found, audio clips disabled")
endif()
