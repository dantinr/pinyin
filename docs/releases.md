# Windows 发布流程

当前推荐发布带离线安装入口的 x64 ZIP。版本号以 `CMakeLists.txt` 的 `project(... VERSION ...)` 为准，DLL 文件属性和包名自动同步。当前版本为 `0.3.0`。

## 本机打包

安装 Visual Studio C++ 桌面开发工具、CMake 和 PowerShell 7，然后在项目目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

脚本会自动找到 CMake 和 Visual Studio 自带的微软 VC++ x64 离线运行库，使用独立的 `build-package` 目录构建 Release，运行全部 CTest 测试，然后打包并解压验证。安装文件测试使用独立临时目录，验证重复安装、旧 DLL 被锁定时升级、文件校验和路径范围；不请求管理员权限、不实际安装运行库、不修改本机输入法注册。

输出：

```text
out/releases/private-pinyin-0.3.0-windows-x64.zip
out/releases/private-pinyin-0.3.0-windows-x64.zip.sha256
```

包中包含输入法 DLL、词库工具、测试窗口、基础词库、安装/卸载入口、使用说明、版本及文件清单，以及带微软签名的 `vc_redist.x64.exe`。使用明确文件列表打包，个人词库、PDB、测试程序和开发构建目录不进入 ZIP。

可指定工具和输出位置：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1 `
  -BuildDir build-package -OutputDir out/releases `
  -CMakePath "C:\工具\CMake\bin\cmake.exe" `
  -RedistPath "C:\安装包\vc_redist.x64.exe"
```

`-ReleaseTag v0.3.0` 会额外要求版本号一致、源码已提交、标签指向当前提交。普通本地打包允许尚未提交的改动，并在 `manifest.json` 中标记 `sourceDirty: true`；正式版本应从干净的版本标签构建。

## GitHub 自动创建 Release 草稿

先将打包脚本、工作流和相关改动提交并推送到 `main`，再为当前提交创建标签：

```powershell
git tag -a v0.3.0 -m "Private Pinyin 0.3.0"
git push origin v0.3.0
```

仓库的 **Actions → Windows release** 会构建、测试、打包，并创建附带 ZIP 和 SHA-256 的 **Release 草稿**，默认标记为开发预览版。构建机从微软官方地址获取离线运行库并校验微软签名，下载用户安装时不再联网。工作流使用仓库自带的 `GITHUB_TOKEN`，无需另设个人访问令牌。

构建通过后，到 **Releases** 打开草稿，检查版本、说明和附件，再点击 **Publish release**。草稿创建和正式公开发布是两个独立步骤。

也可在 Actions 手动运行 **Windows release → Run workflow**，先下载构建产物；手动运行不会创建 Release。

下一版本只需修改 `CMakeLists.txt` 中的版本号、更新 `packaging/RELEASE_NOTES.md`，提交后推送相应标签。不要给不同源码反复使用同一个版本号。

## 手动上传现有包

也可以在仓库的 **Releases → Draft a new release** 选择或创建 `v0.3.0` 标签，填写标题和说明，并将 ZIP 与 `.sha256` 拖入附件区域。GitHub 自动提供的 Source code 压缩包只包含源码；用户安装需要这里上传的 Windows ZIP。

## 下载后的安装

完整解压 ZIP，双击 `install.cmd`，同意管理员权限提示。安装器将文件复制到 `%ProgramFiles%\PrivatePinyin\0.3.0`，需要时安装附带的 VC++ 运行库，随后注册输入法。安装完成后，下载目录可以删除；用 Win+Space 切换输入法，重新打开应用以加载新版本。

每个版本使用独立目录，升级不会覆盖应用已加载的旧 DLL。卸载时运行当前安装目录中的 `uninstall.cmd`，个人词库和其他程序共用的 VC++ 运行库会保留。应用退出后，可手动删除旧版本目录。

发布包本身的构建、文件复制、完整性检查、解压执行和 COM 加载已可自动验证；首次正式发布前仍需在干净的 Windows x64 机器上实际安装、切换、升级和卸载，确认 UAC 和目标应用表现。程序目前没有代码签名，当前 Release 定位为开发预览版。

参考：[GitHub Release 管理](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)、[微软 VC++ 运行库部署](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)。
