GLAD 加载器未自动生成（当前环境无网络）。

请二选一获取 GLAD 3.3 Core 文件，放到本目录，结构须为：
  third_party/glad/include/glad/glad.h
  third_party/glad/include/KHR/khrplatform.h
  third_party/glad/src/glad.c

方式 A（推荐，零依赖）：
  1. 打开 https://glad.dav1d.de/
  2. 参数：Language=C/C++，Specification=OpenGL，API→gl=3.3，Profile=Core
  3. 勾选 "Generate a loader"，点 Generate，下载 glad.zip
  4. 解压，把 include/ 和 src/ 的内容覆盖到本目录下

方式 B（走 vcpkg）：
  vcpkg install glad --triplet x64-windows
  然后到 vcpkg installed 目录把 glad.c 复制到本目录 src/，确认 glad.h / khrplatform.h 在 include/ 下
