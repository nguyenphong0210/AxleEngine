# Kế Hoạch Refactor: Tách Biệt Camera & Input Khỏi `EditorLayer` (Chuẩn Kiến Trúc Unity / Unreal)

Bạn có thể copy toàn bộ nội dung dưới đây và dán trực tiếp vào khung chat của **Antigravity (Gemini 3.8 Flash)** để Agent tự động đọc codebase, lập kế hoạch thực thi và sửa code theo từng bước.

## Prompt Giao Việc Cho Antigravity Agent

### 1. Bối Cảnh & Mục Tiêu (Context & Objective)
Hiện tại trong `EditorLayer.cpp` của **Axle Engine**, phần **Editor Logic** và **Gameplay Runtime Logic** đang bị trộn lẫn:
1. **Camera bị hardcode:** `EditorLayer` dùng chung một `m_CameraController` (`PerspectiveCameraController`) cho cả `Edit Mode` và `Play Mode`. Toàn bộ thuật toán **Chase / Orbit Camera** đuổi theo xe (`m_OrbitYaw`, `m_OrbitPitch`, `dist = 7.5f`, tự xoay theo vận tốc xe) đang nằm cứng trong `EditorLayer::OnUpdate()`.
2. **Gameplay Input bị hardcode:** Đoạn `// Viewport Direct Driving Input` trong `EditorLayer::OnUpdate()` đang đọc trực tiếp phím `W/A/S/D/Up/Down/Left/Right/Space` và ép điều khiển chiếc xe đầu tiên trong `m_Scene->GetRegistry().view<Axle::VehicleComponent>()`.
3. **Bug Input Polling trong `EditorLayer::OnUpdate()`:**
   - Nhánh `else` của phím `F11` đang gán nhầm `m_F1KeyWasPressed = false;` thay vì `m_F11KeyWasPressed = false;` (khiến `F11` chỉ bấm được 1 lần).
   - Biến `m_OKeyWasPressed` bị dùng chung giữa phím `O` đơn lẻ (Step 1 frame khi Pause) và tổ hợp `Ctrl + O` (Open Scene / Open Project), gây kẹt phím `Ctrl + O`.

**Mục tiêu:** Tách hoàn toàn Gameplay Camera và Gameplay Input ra khỏi `EditorLayer` theo kiến trúc chuẩn của Unity/Unreal, giữ nguyên 100% tính năng hiện có (Deep Copy Scene ở Phase 18.2, Reflection ở Phase 13, Hot-Reload `AxleScripts.dll`, Shadow Mapping, Skybox, ImGuizmo).

### 2. Ràng Buộc Kiến Trúc (Architecture Constraints)
* **Không làm hỏng cơ chế Safe Scene State (Phase 18.2):** Mọi Component mới thêm vào (`CameraComponent`) bắt buộc phải được copy trong `Scene::Copy()`, serialize trong `SceneSerializer` và `AssemblySerializer`.
* **Không phụ thuộc ngược:** `EditorLayer` không được chứa bất kỳ hằng số hay logic điều khiển xe (`WASD`) hoặc `ChaseCamera` nào. Mọi logic điều khiển runtime phải nằm trong `AxleScripts` (`ScriptableEntity`).
* **Fallback an toàn khi Play:** Nếu người dùng bấm `Play (F5)` trong một Scene cũ chưa có Entity nào mang `CameraComponent`, `EditorLayer` phải tự động fallback về `m_CameraController` (Editor Camera) để không bị màn hình đen, đồng thời hiện badge thông báo nhẹ trên góc Viewport.

### 3. Các Giai Đoạn Thực Thi Chi Tiết (Step-by-Step Execution Plan)

| Giai đoạn | Hạng mục công việc | Các file cần kiểm tra & cập nhật |
| :--- | :--- | :--- |
| **Phase 1** | Thêm `CameraComponent` vào ECS, Inspector & Serializer | `Components.hpp`, `Scene.hpp`, `Scene.cpp`, `SceneSerializer.cpp`, `AssemblySerializer.cpp`, `SceneHierarchyPanel.cpp` |
| **Phase 2** | Chuyển Driving Input & Chase Camera sang `AxleScripts` | Thư mục scripts của `AxleScripts` (`VehiclePlayerController.hpp/.cpp`, `VehicleChaseCamera.hpp/.cpp`) |
| **Phase 3** | Dọn dẹp `EditorLayer` & hỗ trợ chế độ `Eject` (Scene View vs Game View) | `EditorLayer.hpp`, `EditorLayer.cpp` |
| **Phase 4** | Sửa 2 bug phím tắt (`F11`, `Ctrl+O`) & kiểm thử toàn diện | `EditorLayer.hpp`, `EditorLayer.cpp` |

#### Phase 1: Bổ sung `CameraComponent` vào hệ thống ECS
1. **Định nghĩa `Axle::CameraComponent` trong `Components.hpp`:**
   ```cpp
   struct CameraComponent {
       bool Primary = true;
       float FOV = 45.0f;
       float NearClip = 0.1f;
       float FarClip = 1000.0f;
       bool FixedAspectRatio = false;
   };
   ```
2. **Cập nhật `Scene::Copy()` và `Scene` helpers (`Scene.hpp` / `Scene.cpp`):**
   - Đảm bảo `CameraComponent` được đăng ký trong danh sách các Component được copy sang `m_RuntimeScene` khi gọi `Axle::Scene::Copy(m_EditorScene)`.
   - Thêm hàm tiện ích `Axle::Entity GetPrimaryCameraEntity()` trong `Axle::Scene` để trả về Entity đầu tiên có `CameraComponent` với `Primary == true` (và có `TransformComponent`).
   - Kiểm tra chữ ký `m_Scene->OnUpdate(...)`: Nếu hiện tại `Scene::OnUpdate` chỉ nhận `const Axle::PerspectiveCamera&`, hãy thêm overload hoặc cho phép truyền trực tiếp `const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPos` (hoặc tạo một `PerspectiveCamera` tạm từ `TransformComponent` + `CameraComponent` của Main Camera).
3. **Cập nhật Serialization (`SceneSerializer.cpp` & `AssemblySerializer.cpp`):**
   - Lưu và đọc `CameraComponent` (`Primary`, `FOV`, `NearClip`, `FarClip`, `FixedAspectRatio`) trong file `.axlescene` và `.axle`.
4. **Cập nhật UI Inspector (`SceneHierarchyPanel.cpp`):**
   - Thêm mục `"Camera"` vào menu `Add Component` và vẽ UI chỉnh `Primary`, `FOV`, `NearClip`, `FarClip` trong bảng Properties.

#### Phase 2: Tách Gameplay Input & Chase Camera sang `AxleScripts`
Hãy kiểm tra cách các script hiện tại trong `AxleScripts` đang đăng ký với `Axle::ScriptRegistry` và macro/hàm Reflection (Phase 13), sau đó tạo 2 script độc lập:

1. **Script 1: `VehiclePlayerController` (Gắn vào Entity xe có `VehicleComponent`):**
   - Trong `OnUpdate(Axle::Timestep ts)`:
     - Kiểm tra nếu Entity có `Axle::VehicleComponent`.
     - Đọc input từ `Axle::Input::IsKeyPressed`:
       - `W` / `Up` -> `throttle += 1.0f`
       - `S` / `Down` -> `throttle -= 1.0f`
       - `A` / `Left` -> `steer -= 1.0f`
       - `D` / `Right` -> `steer += 1.0f`
       - `Space` -> `handbrake = true`
     - Gọi `vc.SetDriverInput(throttle, steer, brake, handbrake)` và đồng bộ với `GetScene()->GetRuntimeVehicle(GetEntity())`.
     - *(Lưu ý)*: Khi không bấm phím nào, đảm bảo trả `throttle = 0.0f, steer = 0.0f, brake = 0.0f, handbrake = false` để xe tự nhả ga/trả lái mượt mà.

2. **Script 2: `VehicleChaseCamera` (Gắn vào Entity `"Main Camera"`):**
   - Expose các trường qua hệ thống Reflection của `ScriptRegistry` để chỉnh được trên Inspector:
     - `Distance` (mặc định `7.5f`, min `2.0f`, max `30.0f`)
     - `HeightOffset` (mặc định `0.8f`, min `-2.0f`, max `5.0f`)
     - `MouseSensitivity` (mặc định `0.25f`, min `0.05f`, max `2.0f`)
     - `AutoAlignSpeed` (mặc định `3.5f`, min `0.0f`, max `15.0f`)
   - Biến nội bộ: `m_OrbitYaw = 180.0f`, `m_OrbitPitch = 15.0f`, `m_LastMousePos`, `m_FirstMouseUpdate = true`.
   - Trong `OnUpdate(Axle::Timestep ts)`:
     - Tìm Entity mục tiêu trong `GetScene()` có `Axle::VehicleComponent`.
     - Đọc `Axle::Input::GetMousePosition()` và xoay `m_OrbitYaw`, `m_OrbitPitch` khi giữ chuột phải hoặc chuột trái.
     - Lấy `targetPos`, `targetFwd`, `targetSpeed` từ `GetScene()->GetRuntimeVehicle(target)` (hoặc `TransformComponent`).
     - Tự động xoay `m_OrbitYaw` bám theo đuôi xe khi không kéo chuột và `targetSpeed > 0.5f`.
     - Tính `camPos` và góc `pitch`, `yaw`, sau đó gán trực tiếp vào `GetComponent<Axle::TransformComponent>().Translation` và `Rotation` của Entity `"Main Camera"`.

#### Phase 3: Làm sạch `EditorLayer` (Tách bạch Editor Camera & Runtime Camera)
1. **Xóa bỏ code Gameplay khỏi `EditorLayer.hpp` và `EditorLayer.cpp`:**
   - Xóa khối `// Viewport Direct Driving Input` trong `EditorLayer::OnUpdate()`.
   - Xóa khối tính toán `chaseTarget`, `m_OrbitYaw`, `m_OrbitPitch`, `m_LastMousePos`, `m_FirstMouseUpdate` trong `EditorLayer`.
2. **Đổi cơ chế `m_ChaseCamera` thành `m_UseEditorCameraInPlay` (Chế độ Eject / Free-Fly khi đang Play):**
   - Mặc định khi vào `OnScenePlay()`: `m_UseEditorCameraInPlay = false` (tức là nhìn qua `"Main Camera"` của Scene giống Game View của Unity).
   - Khi người dùng bấm phím `C` (hoặc nút trên Toolbar `"Cam: Game [C]"` / `"Cam: Eject Free-Fly [C]"`): đảo giá trị `m_UseEditorCameraInPlay`.
   - Trong `EditorLayer::OnUpdate()`:
     - Nếu `m_SceneState != SceneState::Play || m_UseEditorCameraInPlay`:
       - Cho phép điều khiển `m_CameraController->OnUpdate(ts)` khi Viewport được hover/focus và không dùng `ImGuizmo`.
       - Sử dụng `m_CameraController->GetCamera()` làm Camera render chính.
     - Nếu `m_SceneState == SceneState::Play && !m_UseEditorCameraInPlay`:
       - Tìm `camEntity = m_Scene->GetPrimaryCameraEntity()`.
       - Nếu `camEntity` hợp lệ: Lấy `worldTransform = m_Scene->GetWorldTransform(camEntity)`, suy ra `viewMatrix = glm::inverse(worldTransform)`, `camPos = glm::vec3(worldTransform[3])`, và tính `projMatrix` từ `CameraComponent` + tỷ lệ khung hình `m_ViewportWidth / m_ViewportHeight`. Dùng Camera này để render `m_Scene`, `m_Skybox`, và tính `focusCenter` cho `m_ShadowMap`.
       - Nếu không tìm thấy `camEntity`: Fallback về `m_CameraController->GetCamera()` và vẽ một badge cảnh báo nhỏ trên Viewport `"NO MAIN CAMERA IN SCENE (USING EDITOR CAMERA)"`.
3. **Khởi tạo sẵn `"Main Camera"` trong `NewScene()` và `OpenScene()`:**
   - Trong `EditorLayer::NewScene()`: Tự động tạo thêm Entity `"Main Camera"` tại `(0.0f, 6.0f, 18.0f)`, góc xoay `(-12.0f, 0.0f, 0.0f)`, gắn sẵn `CameraComponent{ .Primary = true }`.
   - Trong `EditorLayer::OpenScene()`: Nếu file scene cũ được mở chưa có Entity nào mang `CameraComponent`, tự động tạo một `"Main Camera"` mặc định (tương tự cách `OpenScene` đang tự động kiểm tra và tạo `"Sun Light"` / `"Skybox"`).

#### Phase 4: Sửa 2 Bug Input trong `EditorLayer::OnUpdate()`
1. **Sửa bug phím `F11`:**
   ```cpp
   if (Axle::Input::IsKeyPressed(Axle::KeyCode::F11)) {
       if (!m_F11KeyWasPressed) {
           auto& window = Axle::Application::Get().GetWindow();
           if (window.IsMaximized()) window.Restore();
           else window.Maximize();
           m_F11KeyWasPressed = true;
       }
   } else {
       m_F11KeyWasPressed = false; // Đã sửa từ m_F1KeyWasPressed = false
   }
   ```
2. **Sửa bug xung đột phím `O` (Step Frame) và `Ctrl + O` (Open Scene):**
   - Ở khối kiểm tra phím `O` để Step 1 frame khi Pause, thêm điều kiện `!isCtrlDown`:
     ```cpp
     if (!isCtrlDown && Axle::Input::IsKeyPressed(Axle::KeyCode::O)) {
         if (!m_OKeyWasPressed) {
             if (m_SceneState == SceneState::Pause) {
                 m_StepSingleFrame = true;
             }
             m_OKeyWasPressed = true;
         }
     } else if (!isCtrlDown) {
         m_OKeyWasPressed = false;
     }
     ```
   - Thêm biến riêng `bool m_CtrlOKeyWasPressed = false;` trong `EditorLayer.hpp` cho tổ hợp `Ctrl + O` (và `Ctrl + Shift + O`).

### 4. Tiêu Chí Kiểm Thử Hoàn Thành (Definition of Done)
Sau khi sửa xong, hãy kiểm tra lại toàn bộ các mục sau:
1. Build thành công cả target Engine/Editor và `AxleScripts.dll` mà không có lỗi biên dịch hay cảnh báo biến thừa (`m_OrbitYaw`, `m_OrbitPitch` đã được dọn khỏi `EditorLayer.hpp`).
2. Bấm `New Scene (Ctrl+N)` sinh ra đủ 3 Entity mặc định: `"Main Camera"`, `"Directional Light"`, `"Skybox Environment"`.
3. Gắn `VehiclePlayerController` vào Entity xe và `VehicleChaseCamera` vào `"Main Camera"`, bấm `Play (F5)`:
   - Xe chạy bình thường bằng `WASD / Space`.
   - `"Main Camera"` tự động bám theo xe và xoay chuột mượt mà.
   - Bấm `C` trong lúc Play chuyển đổi tức thì giữa góc nhìn `"Main Camera"` (Game View) và góc nhìn bay tự do của Editor (`Eject Free-Fly`).
   - Bấm `Stop (F7)` trả toàn bộ vị trí xe và `"Main Camera"` về đúng trạng thái trước khi Play (nhờ `Scene::Copy`).
