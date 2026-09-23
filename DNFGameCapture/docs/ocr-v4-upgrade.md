# OCR v4 升级

双击 `upgrade-ocr-v4.bat`，脚本会自动定位 DNFGameCapture 安装目录，停止 OCR，备份旧的 v3 模型，然后安装 PP-OCRv4 模型。

如果程序没有运行，脚本会提示输入安装目录。升级完成后重新启动 DNFGameCapture 即可。

旧模型会保存在 `UmiOCR-data/plugins/win7_x64_PaddleOCR-json/models/backup-v3-日期时间`，DNF 主程序和个人配置不会被修改。
