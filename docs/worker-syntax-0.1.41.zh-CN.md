# 0.1.41 本地格式诊断依赖及集成

仅检查语法，支持扩展名 JSON、JSONC、XML、YAML/YML、TOML；不调用远端工具，不改变保存、撤销和 dirty 语义。JSONC 对注释及尾逗号进行等 UTF-16 长度遮盖后交给 Qt JSON 解析器；XML 使用 QXmlStreamReader，不提供外部实体解析器；YAML 使用 libyaml 事件接口，不实例化标签或扩展别名；TOML 使用 toml++ 的内存解析接口。错误位置转换为 QTextDocument 的 UTF-16 单位。UI 每次编辑先清除旧错误，300 ms 停顿后检查；最多 1 Mi UTF-16 单位，全局同一时刻只运行一个解析，忙时定时重试而不排队缓存正文；以递增修订拒绝旧结果。

固定依赖（均 MIT，许可证见 `third_party/licenses/libyaml.txt`、`third_party/licenses/tomlplusplus.txt`）：

* libyaml 0.2.5: https://codeload.github.com/yaml/libyaml/tar.gz/refs/tags/0.2.5 ; SHA256 `fa240dbf262be053f3898006d502d514936c818e422afdcf33921c63bed9bf2e`。
* toml++ 3.4.0: https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0 ; SHA256 `8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155`。

集成时禁用 BUILD_TESTING，CMake 4 需在 libyaml 配置前设置 `CMAKE_POLICY_VERSION_MINIMUM=3.5`；注册 `src/core/syntaxdiagnostic.cpp`、`.h` 至 `rabbitshell_core` 并私有链接 `yaml`、`tomlplusplus::tomlplusplus`。`tests/test_syntaxdiagnostic.cpp` 链接 `rabbitshell_ui` 和 `Qt6::Test`，无界面 CI 使用 `QT_QPA_PLATFORM=offscreen`。
