# New model intake template / 新机型接入模板

Complete this file in a pull request before a model moves beyond
profile-only. Do not include device serial numbers, MAC addresses, or other
personal data.

## Identity / 身份信息

- Plugin ID:
- Marketing name:
- Exact DMI product name(s):
- Board revision:
- CPU architecture:
- Firmware version and build date:

## Initial support level / 初始支持等级

- [ ] profile-only
- [ ] read-only
- [ ] supported
- [ ] blocked

Reason / 原因：

## Controller and transport / 控制器与传输层

- Controller chip and revision:
- Transport: EC / Super I/O / GPIO / other
- Stock kernel modules or services:
- Conflicting modules/services:
- Required privileges:

## Validated capabilities / 已验证能力

| Capability | Read tested | Write tested | Safe range | Firmware tested | Notes |
| --- | --- | --- | --- | --- |
| Fan |  |  |  |  |  |
| LED |  |  |  |  |  |
| Power recovery |  |  |  |  |  |
| Other |  |  |  |  |  |

## Evidence / 验证证据

- Test date:
- Test method:
- Expected versus observed result:
- Recovery/rollback procedure:
- Logs or public, non-sensitive references:

## Catalog change / 目录变更

- [ ] Added or updated models/compatibility.json.
- [ ] DMI strings are exact and captured from the tested device.
- [ ] Every unsupported capability remains unverified or blocked.
- [ ] Documentation describes conflicts and firmware limits.
