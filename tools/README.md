# Tools

Diagnostic and testing utilities for the Verbifrost stack.

## Planned tools

| Tool | Equivalent | Purpose |
|------|-----------|---------|
| `vfinfo` | `ibv_devices` / `ibv_devinfo` | List RDMA devices and their capabilities |
| `vf_ping` | `ibping` / `rdma_ping` | RDMA round-trip latency test |
| `vf_bw` | `ib_write_bw` / `ib_send_bw` | RDMA bandwidth benchmark |
| `vf_rcat` | `rping` | RDMA copy (cat over RDMA) |
| `vf_stat` | `ibstat` | Device state and port info |

All tools will use the standard `ibv_*` API from `libverbifrost`.
