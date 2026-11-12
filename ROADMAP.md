# AxleEngine Roadmap

Bản lộ trình thực thi tổng thể duy nhất của AxleEngine.
Toàn bộ dự án được quản lý thống nhất theo các **Milestone (Cột mốc)**. Khái niệm `OPT` và `Phase` đã được gộp làm một: mỗi Milestone quản lý các **Phase** tương ứng. Mỗi Milestone sở hữu cặp tài liệu tiêu chuẩn: `PLAN.md` (kế hoạch thực hiện & checklist) và `ARCHITECTURE.md` (thiết kế kiến trúc) đặt tại `docs/phases/`.

---

## 1. Danh Mục Các Milestone Tổng Thể

| Milestone | Tên Milestone | Phạm vi & Trạng thái | Tài liệu Kiến trúc & Kế hoạch |
| :---: | :--- | :--- | :---: |
| **00** | **Kiến trúc Nền tảng (Architecture Foundation)** | **Đã hoàn thành:** 41 Phase lịch sử (Milestones 1–10) + củng cố kiến trúc lõi (Phase 0.0 – 0.9).<br>**Đang mở:** 3 cổng nghiệm thu cuối của Phase 0.10. | [Kế hoạch](docs/phases/PHASE_00_FOUNDATION/PLAN.md) · [Kiến trúc](docs/phases/PHASE_00_FOUNDATION/ARCHITECTURE.md) |
| **01** | **Nền tảng Render & Vật liệu (Render & Material Foundation)** | **Đã hoàn thành:** Đa luồng Render Thread, VRAM Aliasing (Phase 1.1) và Material Compiler DXC (Phase 1.2).<br>**Tạm hoãn:** Triển khai render song song mặc định (Phase 1.3). | [Kế hoạch](docs/phases/PHASE_01_RENDER_FOUNDATION/PLAN.md) · [Kiến trúc](docs/phases/PHASE_01_RENDER_FOUNDATION/ARCHITECTURE.md) |
| **02** | **Chiếu sáng & Vật liệu Điện ảnh (Lighting & Material Fidelity)** | **Đã hoàn thành:** CSM, HDR/IBL, Bóng đổ Point/Spot, Sửa Viewport (Phase 2.1 – 2.3).<br>**Hàng đợi:** GGX Kulla-Conty, GTAO, Froxel Fog, DDGI Probes, LTC, TAA. | [Kế hoạch](docs/phases/PHASE_02_LIGHTING_MATERIALS/PLAN.md) · [Kiến trúc](docs/phases/PHASE_02_LIGHTING_MATERIALS/ARCHITECTURE.md) |
| **03** | **Hình học & Tầm nhìn (Geometry & Visibility)** | **Đề xuất:** Đường ống autoLOD ngoại tuyến qua `meshoptimizer` (Phase 3.1) và Virtual Geometry Nanite-style với Task/Mesh Shaders (Phase 3.2 – 3.5). | [Kế hoạch](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/PLAN.md) · [Kiến trúc](docs/phases/PHASE_03_GEOMETRY_VISIBILITY/ARCHITECTURE.md) |
| **04** | **Quy mô Mô phỏng (Simulation Scale)** | **Đề xuất:** Phân tầng tầm quan trọng Significance, lập lịch so le modulo, lưới không gian, vùng ngủ đông 0% CPU, nội suy chuyển động (Phase 4.1 – 4.4). | [Kế hoạch](docs/phases/PHASE_04_SIMULATION_SCALE/PLAN.md) · [Kiến trúc](docs/phases/PHASE_04_SIMULATION_SCALE/ARCHITECTURE.md) |
| **05** | **Công cụ Studio & Sáng tạo (Content & Authoring Tools)** | **Đề xuất:** Animation Studio (Phase 5.1), VFX Studio & GPU Particles (Phase 5.2), Material Graph UI Canvas (Phase 5.3), Cooker & Pak (Phase 5.4). | [Kế hoạch](docs/phases/PHASE_05_CONTENT_TOOLING/PLAN.md) · [Kiến trúc](docs/phases/PHASE_05_CONTENT_TOOLING/ARCHITECTURE.md) |
| **06** | **Hiệu năng & Phát hành (Performance & Rollout)** | **Đề xuất:** Ma trận benchmark chuẩn hóa, tương đương hình ảnh đa backend PSNR/SSIM, phục hồi lỗi mất thiết bị GPU và nghiệm thu phát hành (Phase 6.1 – 6.6). | [Kế hoạch](docs/phases/PHASE_06_ROLLOUT/PLAN.md) · [Kiến trúc](docs/phases/PHASE_06_ROLLOUT/ARCHITECTURE.md) |

---

## 2. Toàn Bộ Chi Tiết: Những Việc Đã Từng Làm vs Những Việc Chưa Làm

### Phần A: Những Việc ĐÃ TỪNG LÀM (`[x]` Hoàn thành)

#### Milestone 00: Kiến trúc Nền tảng
- [x] **41 Phase lịch sử (Giai đoạn tiền đề Milestones 1 – 10)**:
  - *Milestone 1*: UI/Telemetry (Phase 1), Jolt Physics (Phase 2), Scene & Transform (Phase 3), Vehicle prototype (Phase 4).
  - *Milestone 2*: Editor Lifecycle Play/Pause (Phase 5), 3D Viewport & Gizmo (Phase 6), Obstacle Playground (Phase 7).
  - *Milestone 3*: C++ Scripting (Phase 8), Assimp Asset Ingest (Phase 9), JSON Scene Serializer (Phase 10), Cook-Torrance PBR (Phase 11), Prefab Assembly `.axle` (Phase 12), Mechanical Assembly (Phase 13).
  - *Milestone 4*: EnTT ECS & Scene Graph (Phase 14), Hierarchy Tree UI (Phase 15), Properties Reflection (Phase 16), Content Browser (Phase 17), Drag-Drop & Script Hot-Reload DLL (Phase 18), enkiTS Multithreaded Job System (Phase 19).
  - *Milestone 5*: Component-Driven Architecture (Phase 20), Decoupled Scene Renderer (Phase 21), Standalone `AxleRuntime.exe` (Phase 22), Asset Packaging & VFS (Phase 23).
  - *Milestone 6*: Decoupled Material Flyweight (Phase 24), AAA Soft Shadows (Phase 25), HDR IBL Skybox (Phase 26), miniaudio 3D Spatial Audio (Phase 27), Gameplay Polish (Phase 28).
  - *Milestone 7*: Spline & Terrain Modular Plugins (Phase 29), Skeletal Animation CPU Evaluation (Phase 30).
  - *Milestone 8*: Advanced Particle VFX System (Phase 31), NVIDIA NRI Modern RHI (Phase 32), Forward+ Multi-Backend Viewport (Phase 33), Declarative Canvas2D UI (Phase 34).
  - *Milestone 9*: Vulkan 1.3 Backend Parity (Phase 35), Unified `.axleasset` Architecture (Phase 36), Raw Media Ingestion (Phase 37).
  - *Milestone 10*: Visual Material Studio & Dynamic HLSL Reflection (Phases 38, 38.1, 38.2).
- [x] **Phase 0.0 (OPT-00)**: Tách mã nguồn Vehicle cũ thành Plugin độc lập, bảo toàn dữ liệu scene cũ.
- [x] **Phase 0.1 (OPT-01)**: Bộ kiểm tra kiến trúc nghiêm ngặt `check_architecture.py` (chính sách Zero-Allowance, kiểm tra 576+ file).
- [x] **Phase 0.2 (OPT-02)**: Chuẩn hóa hệ thống đa luồng `enkiTS` (`JobSystem`), cô lập worker an toàn.
- [x] **Phase 0.3 (OPT-03)**: Phân định ranh giới Jolt Physics 3D, tách biệt giao diện debug `Axle::PhysicsDebug`.
- [x] **Phase 0.4 (OPT-04)**: Nền tảng thiết bị Modern RHI (D3D12/Vulkan qua NRI), quản lý epoch và vòng đời tài nguyên GPU.
- [x] **Phase 0.5 (OPT-05)**: Đường ống render thống nhất: CPU snapshot bất biến `RenderWorld`, `RenderService` và `RenderGraph`.
- [x] **Phase 0.6 (OPT-06)**: Kiến trúc Plugin SDK, nạp động DLL và lifecycle hooks (`OnLoad`/`OnUnload`).
- [x] **Phase 0.7 (OPT-07)**: File mô tả dự án `.axleproject`, tự động phát hiện runtime.
- [x] **Phase 0.8 (OPT-08)**: Quản lý tài sản VFS, định danh nhúng `# AxleFileIdentity: 1 <UUID>` (loại bỏ hoàn toàn file rác `.meta`).
- [x] **Phase 0.9 (OPT-09)**: Cooker đóng gói độc lập xuất file nén `Data.axlepak`.

#### Milestone 01: Nền tảng Render & Vật liệu
- [x] **Phase 1.1 (OPT-11)**: Concurrency Render Thread và Transient VRAM Memory Aliasing (gói `C11-01` đến `C11-07`).
- [x] **Phase 1.2 (OPT-12)**: Trình biên dịch Material Graph DXC (HLSL/DXIL/SPIR-V), Layout Cache và cơ chế dự phòng last-known-good (`C12-01` đến `C12-06`).

#### Milestone 02: Chiếu sáng & Vật liệu Điện ảnh (Baseline)
- [x] **Phase 2.1**: Bóng đổ phân tầng Cascaded Shadow Mapping (CSM 4-cascade, PCF 5x5 trên D3D12/Vulkan) (`C13-01`).
- [x] **Phase 2.2**: Môi trường HDR & Ánh sáng gián tiếp Split-Sum IBL (Prefilter cubemap, BRDF LUT 2D) (`C13-02`).
- [x] **Phase 2.3**: Bóng đổ cục bộ Point/Spot Light và căn chỉnh Viewport Fixture (winding order hình cầu, mặt sàn $Y=0.0$, kết nối `Studio_Softbox.hdr`).

---

### Phần B: Những Việc CHƯA LÀM (`[ ]` Đang chờ / Đề xuất)

#### Milestone 00: Củng cố Kiến trúc (3 Cổng nghiệm thu cuối)
- [ ] **Phase 0.10 (OPT-10 Open Gates)**:
  - [ ] Nghiệm thu bản build Release sản phẩm đầy đủ (AxleEditor & AxleRuntime không có cờ gỡ lỗi).
  - [ ] Stress test phiên xuất gói tài sản độc lập thời gian dài (Detached packaging soak).
  - [ ] Nghiệm thu tương tác UI thực tế trên cửa sổ native.

#### Milestone 01: Nền tảng Render (Mở rộng)
- [ ] **Phase 1.3**: Kích hoạt đường ống Render đa luồng song song làm mặc định toàn engine.
- [ ] **Phase 1.4**: Bộ hạ cấp GLSL 4.50 chuyên dụng cho Material Graph trên OpenGL.

#### Milestone 02: Chiếu sáng & Vật liệu Điện ảnh (Fidelity Roadmap)
- [ ] **Phase 2.4**: Bù năng lượng phản xạ Multi-Bounce GGX (Kulla-Conty) phục hồi độ sáng kim loại thô.
- [ ] **Phase 2.5**: Nướng thời gian thực Dynamic Procedural Skybox IBL từ bầu trời khí quyển.
- [ ] **Phase 2.6**: Mở rộng góc specular mặt trời ($0.53^\circ$) làm dịu đốm lóa.
- [ ] **Phase 2.7**: GTAO (Ground-Truth Ambient Occlusion) nửa độ phân giải với bộ lọc song phương.
- [ ] **Phase 2.8**: Screen-Space Contact Shadows (bóng đổ tiếp xúc góc hẹp chân tường/vật thể).
- [ ] **Phase 2.9**: Tạo Hi-Z Depth Pyramid & Screen-Space Reflections (SSR) phản chiếu mặt sàn.
- [ ] **Phase 2.10**: Reflection Probes cục bộ có bù thị sai (Parallax-corrected local cubemaps).
- [ ] **Phase 2.11**: Đèn diện tích LTC (Rect & Disk Studio Area Lights).
- [ ] **Phase 2.12**: Sương mù thể tích Froxel & God Rays ($160 \times 90 \times 64$, trước đây là C13-04).
- [ ] **Phase 2.13**: Chiếu sáng gián tiếp động DDGI Probes với Chebyshev visibility (trước đây là C13-05).
- [ ] **Phase 2.14**: Hậu kỳ điện ảnh (Kawase Bloom, Histogram Auto-Exposure, ACES, trước đây là C13-06).
- [ ] **Phase 2.15**: Khử răng cưa thời gian TAA (Halton jitter, reprojection, YCoCg clamping).
- [ ] **Phase 2.16**: Mô hình vật liệu phức tạp (SSS xuyên sáng da/sáp, Sheen sợi vải nhung, Beer-Lambert hấp thụ thủy tinh & tán sắc ánh sáng).
- [ ] **Phase 2.17**: Nghiệm thu ngân sách GPU $3.5\text{ms} - 5.0\text{ms}$ trên GPU phổ thông (trước đây là C13-07).

#### Milestone 03: Hình học & Tầm nhìn (Nanite-style Geometry)
- **Phase 3.1: Đường ống autoLOD ngoại tuyến qua `meshoptimizer`** (trước đây là OPT-16):
  - [ ] `C16-01`: Tự động giảm đa giác sinh chuỗi LOD0..LOD3 khi import mesh (LOD0 100%, LOD1 50%, LOD2 25%, LOD3 10%).
  - [ ] `C16-02`: Ngưỡng tỷ lệ bao phủ màn hình & dither cross-fading tránh pop hình.
  - [ ] `C16-03`: Chọn LOD tối giản cho bóng đổ shadow caster & cache invalidation.
- **Phase 3.2: Virtual Geometry & GPU Culling** (trước đây là OPT-14):
  - [ ] `C14-01`: Bộ đệm GPU Instance Buffer thống nhất (Transform, Bounds, Material ID, Visibility Flags).
  - [ ] `C14-02`: GPU-driven culling (frustum, cone, Hi-Z occlusion) & lệnh vẽ gián tiếp `ExecuteIndirect` / `vkCmdDrawIndexedIndirect`.
  - [ ] `C14-03`: Tạo Meshlet asset ($\\le 64$ đỉnh, $\\le 126$ tam giác) & nón bao định hướng qua `meshopt_buildMeshlets`.
  - [ ] `C14-04`: Phân cấp Cluster DAG & khóa vòng biên chống nứt vỡ hình học 100%.
  - [ ] `C14-05`: Phần cứng Task & Mesh Shaders trên D3D12/Vulkan.
  - [ ] `C14-06`: Tự động phát hiện phần cứng & fallback sang GPU culling vẽ chỉ số cổ điển.
  - [ ] `C14-07`: Nghiệm thu quy mô cảnh dày đặc ($100,000+$ instances, $50\text{M}+$ polygons).

#### Milestone 04: Quy mô Mô phỏng (Simulation Scale & Logic LOD)
*(Trước đây là OPT-15)*
- **Phase 4.1: Định danh Tick & Phân tầng tầm quan trọng**:
  - [ ] `C15-01`: Tick counter đơn điệu trong `SimulationClock`.
  - [ ] `C15-02`: Phân tầng cự ly camera (Tier 1 `< 20m` @ 60/144 Hz, Tier 2 `20-100m` @ 15-30 Hz, Tier 3 `100-250m` @ 1-2 Hz, Tier 4 `> 250m` Dormant).
- **Phase 4.2: Lập lịch so le & Chỉ mục không gian**:
  - [ ] `C15-03`: Lập lịch chia bucket modulo `(Tick + ID) % Interval == 0` giữ ổn định frame-time.
  - [ ] `C15-04`: Lưới băm không gian động 3D ($64\text{m} \times 64\text{m}$) và chuyển dịch thực thể.
- **Phase 4.3: Ngủ đông không gian & Nội suy chuyển động**:
  - [ ] `C15-05`: Vùng ngủ đông `DormantTag` tiêu tốn 0.000% CPU, đánh thức theo sự kiện/va chạm.
  - [ ] `C15-06`: Nội suy vị trí hiển thị mượt mà cho các thực thể tick chậm khi render.
- **Phase 4.4: Tối ưu vật lý & hoạt ảnh**:
  - [ ] `C15-07`: Gợi ý ngủ đông và chuyển sang chuyển động động học (kinematic) cho Jolt Physics.
  - [ ] `C15-08`: API truy vấn cấp độ quan trọng cho plugin.
  - [ ] `C15-09`: Tối ưu tần số cập nhật xương (Animation URO) & Bone LOD (LOD0 full, LOD1 chính, LOD2 khớp gốc).

#### Milestone 05: Công cụ Studio & Sáng tạo (Content & Authoring Tools)
- **Phase 5.1: Skeletal Animation Studio** (trước đây là OPT-17 / Phase 40):
  - [ ] `C17-01`: Trình import FBX/glTF, rig tham chiếu, giữ nguyên bind pose cho kênh không key.
  - [ ] `C17-02`: Dope Sheet Timeline, chỉnh sửa key TRS, copy/paste, undo/redo.
  - [ ] `C17-03`: State Machine Controller, tham số (Float/Int/Bool/Trigger), Blend Space 1D/2D liên tục pha.
  - [ ] `C17-04`: Đưa 128 ma trận xương lên GPU Structured Buffer trên D3D12/Vulkan.
  - [ ] `C17-05`: Bone Sockets gắn vũ khí, mũ giáp, đạo cụ vào xương.
  - [ ] `C17-06`: Foot IK 2 khớp bám mặt đất chống trượt chân.
  - [ ] `C17-07`: Additive Animation & Upper-body mask cho phép vừa chạy vừa bắn/nạp đạn.
- **Phase 5.2: Visual FX Studio** (trước đây là OPT-18 / Phase 39):
  - [ ] `C18-01`: Document đa emitter độc lập `.axlevfx`, viewport preview cô lập hoàn toàn với scene.
  - [ ] `C18-02`: Chế độ tham số nâng cao (Constant, Two Constants, Curve, Two Curves, Gradient).
  - [ ] `C18-03`: Module phát sinh theo quãng đường, trễ xuất hiện, prewarm, lực theo trục, chuyển động quỹ đạo.
  - [ ] `C18-04`: Dải vệt hạt Ribbon & Trail với lịch sử hạt giới hạn.
  - [ ] `C18-05`: Hạt mô hình 3D (Instanced Mesh Particles).
  - [ ] `C18-06`: Va chạm mặt sàn/vật cản & kích hoạt sub-emitter khi sinh/chạm/chết.
  - [ ] `C18-07`: Trộn khung hình phụ Flipbook sub-frame mượt mà.
  - [ ] `C18-08`: Mô phỏng hạt bằng Compute Shader trên GPU & Visual Effect Graph.
- **Phase 5.3: Visual Material Graph UI & Công cụ Scene** (trước đây là OPT-19 / Phase 38.3):
  - [ ] `C19-01`: Canvas nối node trực quan trong Editor (`imnodes`), biên dịch thời gian thực qua DXC.
  - [ ] `C19-02`: Node vật liệu cao cấp (Clearcoat, Anisotropy, SSS, Refraction & Dispersion).
  - [ ] `C19-03`: Gizmo trực quan đặt Reflection Probe trong scene & nướng cubemap.
  - [ ] `C19-04`: Decal Volume chiếu vết đạn, vệt lốp xe, bùn đất.
  - [ ] `C19-05`: Prefab Variant & so sánh thuộc tính diff trực quan.
- **Phase 5.4: Bộ công cụ đóng gói & kiểm tra chất lượng** (trước đây là OPT-Tools):
  - [ ] `CTools-01`: Cooker đóng gói duyệt cây phụ thuộc, nén texture BCn, biên dịch trước shader cache.
  - [ ] `CTools-02`: Đóng gói toàn bộ tài sản vào file nén nguyên khối `Data.axlepak`.
  - [ ] `CTools-03`: Asset Linter chạy dòng lệnh kiểm tra UUID mồ côi, texture thiếu hoặc quá khổ.

#### Milestone 06: Hiệu năng & Phát hành (Performance & Rollout)
*(Trước đây là OPT-20)*
- [ ] **Phase 6.1**: Ma trận benchmark chuẩn hóa (`MaterialReference`, `CarTest`, `OffRoadTrack`, cảnh đa giác dày).
- [ ] **Phase 6.2**: Kiểm thử tương đương hình ảnh đa backend PSNR, SSIM, sai số kênh màu cực đại.
- [ ] **Phase 6.3**: Đo đạc GPU timestamp telemetry (G-Buffer, Shadow, Lighting, Post-Process) & percentiles p50/p95/p99.
- [ ] **Phase 6.4**: Kiểm thử độ tin cậy, stress resize cửa sổ, phục hồi lỗi mất thiết bị GPU (device loss).
- [ ] **Phase 6.5**: Dọn dẹp code shim tạm thời, tắt overhead debug trên bản Release.
- [ ] **Phase 6.6**: Nghiệm thu toàn bộ ma trận (Debug/Release, D3D12/Vulkan/OpenGL) và phát hành chính thức.

---

## 3. Nguyên Tắc Quản Trị & Thực Thi

1. **Thẩm quyền Duy nhất**: `ROADMAP.md` là nguồn thẩm quyền duy nhất về thứ tự thực thi. Khái niệm `OPT` được gộp hoàn toàn vào các **Phase** con thuộc từng **Milestone**.
2. **Quy tắc 2 Tài liệu**: Mỗi Milestone sở hữu duy nhất một file `PLAN.md` và một file `ARCHITECTURE.md` tại `docs/phases/`.
3. **Bằng chứng Nghiệm thu**: Mọi kết quả nghiệm thu lưu tại `docs/reviews/`. Tài liệu lịch sử tham chiếu tại `docs/archive/`.
