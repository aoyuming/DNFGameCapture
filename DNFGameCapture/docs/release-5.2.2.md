# 5.2.2 正式版准备说明

## 变更内容

- 源头预防：公共选手库导入与投稿继续保持先拉取、事务落库、再从最新持久快照重建请求的顺序，避免旧实体身份制造新的归属冲突。
- 部分冲突上报：客户端遇到真实归属冲突时，会把落库后的 V2 快照提交后台作为批量处理证据；该轮仍不计入七天成功检查点。
- 管理员批量名称确认安全边界：批量确认只处理操作者明确选择并再次确认的规范化名称，不把未选名称或过期修订带入处理。
- 持久重定向：服务端保存实体归并后的重定向关系，使旧实体引用继续解析到权威实体。
- 后台保留审核状态与幂等边界；相同冲突资料已经待审时，客户端不会在同一轮创建第二次投稿。

## 发布顺序

本版本必须采用 **server-first** 顺序：先备份并升级服务端，完成本地与公网健康检查及管理员页面验证，再构建和分发 5.2.2 客户端。详细操作、验证和回滚步骤见 `cloud-match-server/README-production-5.2.2.md`。

Task 6 只准备源码、元数据和打包脚本：不自动部署生产服务，不上传 OSS，不发布更新清单，不执行真实冲突批次。任何线上变更和真实批量确认都需要后续单独授权。

## 本地产物

```text
Client archive: deployment-packages/update_v522.zip
Client artifact path: deployment-packages/update_v522.zip
Client bytes: 5235464
Client SHA256: 56DAA46CB9FCCCEE129F0B4BF2905124381F18370ECB4ACC49989B27109F2F87

Server archive: dnf-cloud-match-server-production-5.2.2.zip
Server artifact path: deployment-packages/dnf-cloud-match-server-production-5.2.2.zip
Server bytes: 151233
Server SHA256: 7C62978BE912230F3D19FD9287B70973E14687A746FA030B9EA13C1B744A21E2

Client EXE path: C:\Users\BRO\source\repos\DNFGameCapture\build\client-5.2.2\DNFGameCapture.exe
Client EXE bytes: 7374336
Client EXE SHA256: 568A9BC84B4A1663D6C023BB83611EF989D18E3BD5C4AED14CAEBCF687C2F806
```

## 验证记录

```text
Server: build PASS; test typecheck PASS; 26/26 files and 364/364 tests PASS.
Browser: conflict batch, library admin and library review real-Edge checks PASS; 1366x900, 390x844 and 568x320 conflict dialog checks PASS.
Client/static: 91/91 Node tests PASS; player-library core PASS; cloud authorization gate PASS.
Native build/version: Release x64 PASS with 0 errors and 1 existing C4102 warning; file/product version 5.2.2.0.
Package validation: server ValidateOnly PASS (43 allowlisted entries, serverFirstRequired=true); server ZIP hash verification PASS; client ZIP contains exactly 14 allowlisted runtime entries.
```

最终验证必须确认服务端编译与测试、客户端静态回归、独立 x64 Release 构建、EXE 文件版本 `5.2.2.0`、客户端运行时白名单以及两个归档的实际 SHA256。所有实际路径、数量和摘要只从最终命令输出填写。
