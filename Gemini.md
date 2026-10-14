# AXLE ENGINE - AI AGENT SPECIFICATION (GEMINI.md)

Tài liệu này định nghĩa kiến trúc cốt lõi, quy tắc thiết kế và đặc tả chi tiết cho hệ thống Editor của **Axle Engine** (C++ Game Engine sử dụng kiến trúc **ECS - Entity Component System** và **Dear ImGui**). 

Khi thực thi nhiệm vụ (implement, refactor, hoặc thêm tính năng), AI Agent (**Antigravity**) BẮT BUỘC phải tuân thủ nghiêm ngặt các nguyên tắc kiến trúc trong file này.

---

## 1. NGUYÊN TẮC KIẾN TRÚC CỐT LÕI (CORE ARCHITECTURAL RULES)

Sự phân tách giữa **Dữ liệu thực thi trên RAM (ECS Registry)** và **Tài nguyên trên Ổ cứng (Disk Assets)** là bất khả xâm phạm:

| Khu vực UI | Bản chất trong ECS | Lưu trữ tại | Trách nhiệm chính |
| :--- | :--- | :--- | :--- |
| **Scene Hierarchy** | Danh sách các `Entity` đang sống trong Scene hiện hành. | **RAM** (`ECS Registry`) & Serialize ra file `.scene`. | Quản lý vòng đời Entity, cây phân cấp Cha-Con (`RelationshipComponent`) và tạo nhanh Entity theo Preset. |
| **Properties (Inspector)** | Bộ đọc/ghi dữ liệu (Reflection/UI) của các `Component` thuộc 1 `Entity` đang chọn. | **RAM** (Sparse Set / Component Pools). | Hiển thị, chỉnh sửa dữ liệu `struct` Component, gắn thêm Component hoặc C++ Script vào Entity. |
| **Content Browser** | Trình quản lý tài nguyên (Assets) và hệ thống file dự án (`Assets/`). | **Ổ cứng (Disk)** & quản lý qua `AssetManager` (`UUID`). | Duyệt file, biên dịch Asset thô sang định dạng nhị phân `.axle` (`Compile Assets (F9)`), tạo file tài nguyên mới. |

### Quy tắc Flyweight Pattern cho Asset:
* **TUYỆT ĐỐI KHÔNG** lưu dữ liệu nặng (như mảng Vertex, dữ liệu Texture, âm thanh) trực tiếp bên trong Component của ECS.
* Component trong ECS (ví dụ: `MeshComponent`, `MaterialComponent`) chỉ được phép lưu **`AssetHandle` (UUID 64-bit)**. Dữ liệu thực tế nằm trong `AssetManager` và được tham chiếu từ **Content Browser**.

---

## 2. CẤU TRÚC CORE COMPONENTS & SCRIPTING (C++ SPECS)

Mọi Entity được tạo ra trong Engine phải tuân theo hệ thống Component chuẩn dưới đây:

### 2.1. Các Component nền tảng (Core Components)
~~~cpp
// Định danh duy nhất toàn cục (dùng để lưu/nạp Scene, Prefab và giữ tham chiếu)
struct IDComponent {
    Axle::UUID ID;
};

// Tên hiển thị trên Scene Hierarchy
struct TagComponent {
    std::string Tag = "Entity";
    bool IsActive = true;
};

// Vị trí, góc xoay, tỷ lệ trong không gian
struct TransformComponent {
    glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // Euler angles (radians/degrees)
    glm::vec3 Scale    = { 1.0f, 1.0f, 1.0f };

    glm::mat4 LocalMatrix = glm::mat4(1.0f);
    glm::mat4 WorldMatrix = glm::mat4(1.0f);
};

// Quản lý cây phân cấp Cha - Con (Scene Graph) trong ECS mà không dùng con trỏ mảng động
struct RelationshipComponent {
    Entity Parent      = NullEntity;
    Entity FirstChild  = NullEntity;
    Entity PrevSibling = NullEntity;
    Entity NextSibling = NullEntity;
};
~~~

### 2.2. Hệ thống C++ Scripting (`ScriptableEntity`)
Cho phép lập trình viên viết logic C++ gắn trực tiếp vào Entity như một Component:
~~~cpp
class ScriptableEntity {
public:
    virtual ~ScriptableEntity() = default;

    template<typename T>
    T& GetComponent() { return m_Entity.GetComponent<T>(); }

    template<typename T>
    bool HasComponent() { return m_Entity.HasComponent<T>(); }

protected:
    virtual void OnCreate() {}
    virtual void OnDestroy() {}
    virtual void OnUpdate(float dt) {}
    virtual void OnFixedUpdate(float fixedDt) {}
    virtual void OnImGuiRender() {} // Vẽ các biến public lên bảng Properties

private:
    Entity m_Entity;
    friend class Scene;
    friend class ScriptSystem;
};

struct NativeScriptComponent {
    ScriptableEntity* Instance = nullptr;
    std::string ScriptClassName;

    ScriptableEntity* (*InstantiateScript)() = nullptr;
    void (*DestroyScript)(NativeScriptComponent*) = nullptr;

    template<typename T>
    void Bind(const std::string& className) {
        ScriptClassName = className;
        InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
        DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
    }
};
~~~

---

## 3. ĐẶC TẢ CHI TIẾT 3 BẢNG ĐIỀU KHIỂN (EDITOR PANELS)

### 3.1. Bảng SCENE HIERARCHY (`SceneHierarchyPanel`)
Quản lý danh sách các Entity đang tồn tại trong `Scene` hiện hành.

1. **Hiển thị & Tương tác cây phân cấp (Tree View):**
   * Chỉ duyệt các Root Entity (các Entity có `RelationshipComponent.Parent == NullEntity`) ở cấp ngoài cùng, sau đó đệ quy vẽ các con thông qua `FirstChild` và `NextSibling`.
   * Hỗ trợ thanh tìm kiếm (`Search...`) lọc trực tiếp theo `TagComponent.Tag`.
   * Lưu trữ trạng thái `m_SelectionContext` (Entity đang được chọn) và đồng bộ với Viewport (Gizmo W/E/R) cùng bảng Properties.

2. **Context Menu (Chuột phải) & Thao tác Entity:**
   * `Rename`: Đổi tên `TagComponent`.
   * `Duplicate Entity (Ctrl+D)`: Sao chép toàn bộ các Component của Entity (và các Entity con) sang một Entity mới với `UUID` mới.
   * `Delete Entity (Del)`: Hủy Entity khỏi Registry và gỡ liên kết trong `RelationshipComponent` của Cha/Anh em.
   * `Unparent`: Đưa Entity con ra ngoài làm Root Entity.

3. **Menu `+ Create` (Tạo Entity & Archetype Presets):**
   * **Create Empty Entity:** Tạo Entity với `IDComponent`, `TagComponent`, `TransformComponent`, `RelationshipComponent`.
   * **3D Primitives:** `Cube`, `Sphere`, `Plane`, `Cylinder`, `Capsule` (Gắn sẵn `MeshComponent` trỏ tới Primitive Mesh mặc định của Engine).
   * **Environment & Lighting:**
     * `Skybox Environment` (Gắn `AtmosphericSkyboxComponent` với các thông số Zenith/Horizon/Ground Color, Turbidity, Sun Disk, Presets).
     * `Directional Light` (Gắn `DirectionalLightComponent`).
     * `Point Light` & `Spot Light`.
   * **Camera:** Tạo Entity với `TransformComponent` + `CameraComponent` (FOV, Near, Far, Primary toggle).
   * **Physics / Vehicle Presets:**
     * `Static Obstacle`: Entity có `MeshComponent` + `MeshColliderComponent` + `RigidBodyComponent (Static)`.
     * `Dynamic RigidBody`: Entity có `MeshComponent` + `BoxColliderComponent` + `RigidBodyComponent (Dynamic)`.
     * `Vehicle Rig Preset`: Tạo 1 Entity Cha (`RigidBody` + `VehicleControllerScript`) và 4 Entity Con (`WheelColliderComponent` + `MeshComponent`).

---

### 3.2. Bảng PROPERTIES / INSPECTOR (`PropertiesPanel`)
Hiển thị và chỉnh sửa dữ liệu của đối tượng đang được chọn (`m_SelectionContext`).

1. **Cấu trúc hiển thị khi chọn 1 Entity:**
   * **Header:** Ô Checkbox bật/tắt `IsActive`, ô `InputText` chỉnh sửa `TagComponent.Tag`, và hiển thị `UUID` (chế độ chỉ đọc/mờ).
   * **Quick Drop Zone:** Giữ nguyên khu vực `+ Drop 3D model (.obj / .axle) here to add Mesh` để gán nhanh mô hình 3D vào Entity.
   * **Component Nodes:** Mỗi Component được vẽ trong một `ImGui::CollapsingHeader` (hoặc `TreeNodeEx`), có nút cài đặt (`...` hoặc chuột phải) cho phép `Reset Component` hoặc `Remove Component` (không cho phép xóa `IDComponent` và `TransformComponent`).

2. **Nút `+ Add Component`:**
   * Hiển thị Popup có ô tìm kiếm nhanh các Component đã đăng ký trong Engine, chia theo nhóm:
     * **Rendering:** `MeshComponent`, `MaterialComponent`, `AtmosphericSkyboxComponent`, `DirectionalLightComponent`, `PointLightComponent`, `CameraComponent`.
     * **Physics:** `RigidBodyComponent`, `BoxColliderComponent`, `SphereColliderComponent`, `MeshColliderComponent`, `WheelColliderComponent`.
     * **Scripts:** Danh sách các class kế thừa từ `ScriptableEntity` đã đăng ký trong `ScriptRegistry` (ví dụ: `VehicleController`, `FreeFlyCamera`, `RotatorScript`).
     * **Audio/Effects:** `AudioSourceComponent`, `ParticleSystemComponent`.
   * Khi gọi `AddComponent<T>()`, kiểm tra `if (!entity.HasComponent<T>())` trước khi `emplace`.

3. **Property Reflection / Serialization UI:**
   * Các trường dữ liệu trong Component và `ScriptableEntity` phải được vẽ bằng bộ Widget chuẩn (ví dụ: `DrawVec3Control` cho Position/Rotation/Scale với nút reset X/Y/Z, `ColorEdit3` cho màu sắc, `DragFloat` cho thông số vật lý, và `AssetSlot` cho các trường nhận `AssetHandle`).

---

### 3.3. Bảng CONTENT BROWSER (`ContentBrowserPanel`)
Quản lý toàn bộ file trong thư mục `Assets/` trên ổ cứng và tương tác với `AssetManager`.

1. **Duyệt File & Asset Pipeline:**
   * Duyệt cây thư mục từ gốc `Assets/`, hỗ trợ nút `< Back`, `Refresh`, thanh đường dẫn (Breadcrumbs: `Assets > models`), thanh tìm kiếm `Search assets...` và thanh trượt kích thước Thumbnail (`Thumb: 84 px`).
   * **Nút `Compile Assets (F9)`:** Quét các file nguồn thô (`.obj`, `.fbx`, `.gltf`, `.png`, `.hdr`) và biên dịch sang định dạng nhị phân tối ưu `.axle` kèm theo tạo file `.meta` (lưu trữ `UUID` cố định của Asset đó).
   * Hiển thị nhãn `AXLE` trên góc phải của Thumbnail đối với các Asset đã được biên dịch.

2. **Menu `Create` (Chuột phải trên khoảng trống trong Content Browser):**
   Mọi thao tác tạo tại đây đều **sinh ra file vật lý mới trên ổ cứng** tại thư mục đang mở:
   * **New Folder:** Tạo thư mục mới trên đĩa.
   * **Material (`.amat`):** Tạo file JSON/Binary lưu cấu hình vật liệu (Shader ID, Albedo Color, Metallic, Roughness, Normal/Albedo Texture UUID). Khi click vào file `.amat`, bảng **Properties** sẽ chuyển sang hiển thị trình chỉnh sửa Material đó.
   * **Prefab (`.prefab`):** Tạo hoặc lưu một nhánh Entity mẫu.
   * **Scene (`.scene`):** Tạo một màn chơi mới rỗng. Nhấp đúp vào file `.scene` ở chế độ `EDIT MODE (STOPPED)` sẽ nạp Scene đó lên Scene Hierarchy.
   * **C++ Script (`.h` / `.cpp`):** Mở hộp thoại nhập tên Class (ví dụ: `CarController`), tự động sinh file boilerplate kế thừa từ `Axle::ScriptableEntity` vào thư mục `Assets/scripts/` và đăng ký vào `ScriptRegistry`.
   * **Physics Material (`.physmat`):** Tạo file cấu hình thông số vật lý (Static Friction, Dynamic Friction, Restitution/Bounciness).

---

## 4. CHUẨN GIAO TIẾP DRAG & DROP (IMGUI PAYLOAD SPECIFICATION)

Để 3 bảng điều khiển và Viewport hoạt động liền mạch, mọi thao tác Kéo - Thả (`ImGui::BeginDragDropSource` / `ImGui::BeginDragDropTarget`) phải sử dụng đúng định danh Payload sau:

| Mã Payload ImGui | Dữ liệu truyền đi | Nguồn (Source) | Đích (Target) & Hành vi xử lý |
| :--- | :--- | :--- | :--- |
| `"CONTENT_BROWSER_ITEM"` | Đường dẫn tương đối (`const wchar_t*` hoặc `AssetHandle UUID`) | **Content Browser** | **1. Thả vào Viewport / Scene Hierarchy:**<br>- Nếu là `.axle` / `.obj` (Mesh): Tự động tạo Entity mới + `TransformComponent` + `MeshComponent` gắn model đó.<br>- Nếu là `.prefab`: Khởi tạo (Instantiate) nhánh Entity từ Prefab vào Scene.<br>- Nếu là `.scene`: Mở Scene đó.<br><br>**2. Thả vào Properties:**<br>- Thả vào ô `+ Drop 3D model here` hoặc `MeshComponent`: Gán Mesh cho Entity đang chọn.<br>- Thả `.amat` vào `MaterialComponent`: Gán Material.<br>- Thả `.h/.cpp` vào Properties: Tự động gắn `NativeScriptComponent`. |
| `"SCENE_HIERARCHY_ENTITY"` | `Entity` ID (`uint32_t`) | **Scene Hierarchy** | **1. Thả vào 1 Entity khác trên Scene Hierarchy:**<br>- Thiết lập quan hệ Cha - Con (`RelationshipComponent`).<br><br>**2. Thả vào vùng trống trên Content Browser:**<br>- Serialize toàn bộ Entity (và các con của nó) thành một file `.prefab` mới tại thư mục đang mở.<br><br>**3. Thả vào ô biến `Entity` trong Properties (C++ Script):**<br>- Gán `UUID` của Entity đó vào biến tham chiếu của Script (ví dụ: gán Entity chiếc xe vào biến `TargetEntity` của `CameraFollowScript`). |

---

## 5. QUản LÝ TRẠNG THÁI PLAY / EDIT MODE (SIMULATION LIFECYCLE)

Thanh công cụ phía trên có các trạng thái: `Play (F5)`, `Pause`, `Stop`, `Step`, `Reset (Ctrl+R)`:
1. **Khi nhấn `Play (F5)`:**
   * Engine phải **Clone (Sao chép sâu - Deep Copy)** toàn bộ `EditorScene` hiện tại sang một bản `RuntimeScene` trong RAM.
   * Kích hoạt `OnCreate()` của tất cả `NativeScriptComponent` và khởi chạy `PhysicsSystem` + `ScriptSystem::OnUpdate(dt)`.
   * Mọi thay đổi trong lúc Play chỉ diễn ra trên `RuntimeScene`.
2. **Khi nhấn `Stop`:**
   * Gọi `OnDestroy()` trên `RuntimeScene`, giải phóng `RuntimeScene` và khôi phục lại `EditorScene` ban đầu. Đảm bảo mọi thông số Entity trên Scene Hierarchy và Properties quay về đúng trạng thái trước khi nhấn Play.

---

## 6. LỘ TRÌNH TRIỂN KHAI CHI TIẾT CHO ANTIGRAVITY (DETAILED IMPLEMENTATION ROADMAP)

Khi người dùng yêu cầu triển khai hoặc nâng cấp hệ thống theo tài liệu này, hãy thực hiện tuần tự theo các Phase sau để đảm bảo code luôn biên dịch (compile) thành công và hoạt động ổn định ở từng bước:

### 📍 Phase 1: Chuẩn hóa Core ECS & Relationship Hierarchy (Parent - Child & Scene Graph)
* **Mục tiêu cốt lõi:** Xây dựng nền móng phân cấp hình học vững chắc trong RAM. Đảm bảo mọi Entity đều có UUID cố định và khi một Entity Cha di chuyển/xoay thì tất cả các Entity Con tự động biến đổi theo mà không bị lệch tọa độ.
* **Các công việc kỹ thuật chi tiết:**
  1. **Chuẩn hóa Components:**
     - Đảm bảo mọi Entity khi sinh ra qua `Scene::CreateEntity()` đều có `IDComponent` (UUID 64-bit), `TagComponent`, `TransformComponent`, và `RelationshipComponent`.
     - `RelationshipComponent`: chứa `Entity Parent`, `Entity FirstChild`, `Entity PrevSibling`, `Entity NextSibling` (hoặc cấu trúc liên kết phẳng tối ưu bộ nhớ).
     - `TransformComponent`: Lưu trữ rõ ràng `Translation`, `Rotation` (quat), `Scale` (local), cùng với ma trận cục bộ `LocalMatrix` và ma trận toàn cục `WorldMatrix`.
  2. **Hệ thống tính toán ma trận (Scene Graph Matrix Propagation):**
     - Xây dựng thuật toán duyệt cây phân cấp: Duyệt tất cả các Root Entity (`Parent == NullEntity`), tính `WorldMatrix = LocalMatrix`, sau đó đệ quy tính toán cho toàn bộ cây con: $\text{WorldMatrix}_{\text{Con}} = \text{WorldMatrix}_{\text{Cha}} \times \text{LocalMatrix}_{\text{Con}}$.
     - Cập nhật pipeline render (`Scene::OnUpdate()` và `Scene::RenderScene()`) để sử dụng `WorldMatrix` khi vẽ Mesh, tính toán Bounding Box và đồng bộ sang Jolt Physics.
  3. **World Position Stays Logic (API Reparenting):**
     - Viết hàm `Scene::SetParent(Entity child, Entity parent)`:
       - Kiểm tra chống vòng lặp (Cycle Check): Không cho phép gán Cha làm con của chính Con/Cháu nó.
       - Tính ma trận mới cho con: $\text{Local}_{\text{Con}} = \text{Inverse}(\text{World}_{\text{Cha}}) \times \text{World}_{\text{Con}}$.
       - Decompose ma trận `Local` ra lại Translation, Rotation, Scale để con giữ nguyên tuyệt đối vị trí trong thế giới 3D.
* **Tiêu chuẩn nghiệm thu (Acceptance Criteria):**
  - Gắn 1 khối Cube làm con của 1 khối khác trong code $\rightarrow$ khi xoay khối Cha, khối Con quay quanh khối Cha chính xác.
  - Vị trí thế giới của khối Con không bị nhảy giật khi đổi cha.
  - Dự án build không có cảnh báo (0 warning, 0 error).

---

### 📍 Phase 2: Nâng cấp Scene Hierarchy Panel (Presets, Tree UI & Drag-and-Drop Reparenting)
* **Mục tiêu cốt lõi:** Hoàn thiện giao diện cây phân cấp của Scene Hierarchy, cung cấp đầy đủ các mẫu tạo nhanh (Presets) và cho phép người dùng kéo thả sắp xếp Cha-Con trực tiếp bằng chuột.
* **Các công việc kỹ thuật chi tiết:**
  1. **Menu `+ Create` (Đầy đủ Archetype Presets):**
     - `Empty Entity`: Tạo Entity rỗng cơ bản.
     - `3D Primitives`: `Cube`, `Sphere`, `Cylinder`, `Cone`, `Static Floor Plane` (gắn sẵn Mesh + Collision Shape + RigidBody).
     - `Environment & Lights`:
       - `Atmospheric Skybox`: Tạo Entity riêng mang `SkyboxComponent`.
       - `Directional Sun Light`: Tạo Entity mang `DirectionalLightComponent`.
       - `Point Light` & `Camera` (Scene Camera với FOV, Clip planes).
     - `Vehicle Rig Preset`: Tạo cấu trúc 1 Chassis (Cha) + 4 Bánh xe (Con) canh sẵn vị trí.
  2. **Vẽ cây phân cấp đệ quy (Hierarchical Tree View):**
     - Chỉ vẽ các Root Entity ở ngoài cùng. Sử dụng `ImGuiTreeNodeFlags_OpenOnArrow` để mở/thu gọn nhánh con.
     - Đồng bộ lựa chọn `m_SelectedEntity` với Viewport và Properties.
  3. **Kéo thả sắp xếp Cha - Con (`"SCENE_HIERARCHY_ENTITY"`):**
     - Đăng ký `ImGui::BeginDragDropSource` trên từng Entity node.
     - Đăng ký `ImGui::BeginDragDropTarget` để khi thả Entity A lên Entity B $\rightarrow$ gọi `Scene::SetParent(A, B)`.
  4. **Deferred & Recursive Lifecycle Context Menu:**
     - Chuột phải trên Entity: `Duplicate (Ctrl+D)`, `Delete (Del)`, `Rename (F2)`, `Unparent`.
     - `Delete`: Đưa vào danh sách chờ xóa cuối frame (`m_EntitiesToDestroy`), xóa sạch đệ quy cây con.
     - `Duplicate`: Tạo Entity mới, nhân bản đệ quy toàn bộ con cháu và sinh UUID mới.
* **Tiêu chuẩn nghiệm thu (Acceptance Criteria):**
  - Kéo 1 bánh xe thả vào xe trong Hierarchy $\rightarrow$ Bánh xe biến thành nhánh con.
  - Nhấn Ctrl+D vào một cây xe $\rightarrow$ Nhân bản ra một chiếc xe hoàn chỉnh khác.
  - Xóa Entity không bao giờ gây crash ImGui hay treo ứng dụng.

---

### 📍 Phase 3: Nâng cấp Properties Panel & C++ Scripting Reflection
* **Mục tiêu cốt lõi:** Bảng Properties trở thành Inspector trực quan, hỗ trợ nút `+ Add Component` phân nhóm, và tự động phản chiếu (Reflection) các biến public của C++ Script ra giao diện người dùng.
* **Các công việc kỹ thuật chi tiết:**
  1. **Chuẩn hóa giao diện Component (Inspector Widgets):**
     - Đóng gói từng Component vào `ImGui::CollapsingHeader` có nút gỡ bỏ `Remove Component`.
     - Tối ưu `Transform`: Widget 3 trục X/Y/Z có nút reset màu sắc và kéo chuột chỉnh số mượt mà (`DrawVec3Control`).
  2. **Popup `+ Add Component` phân nhóm chuyên nghiệp:**
     - Có ô tìm kiếm nhanh (Search filter).
     - Chia nhóm rõ ràng: *Rendering* (Mesh, Material, Light, Skybox, Camera), *Physics* (RigidBody, Collider), *Scripts* (Danh sách Script C++), *Audio/VFX*.
  3. **Macro Reflection cho C++ Scripting (`ScriptableEntity`):**
     - Xây dựng Macro hoặc hàm `RegisterFields()` / `AXLE_SCRIPT_FIELDS(...)` cho class kế thừa `ScriptableEntity`.
     - Hỗ trợ các kiểu dữ liệu: `Float`, `Int`, `Bool`, `Vec3`, `String`, và đặc biệt là `Entity Reference` (UUID).
     - Properties Panel tự động đọc danh sách trường này và vẽ Slider / Checkbox tương ứng.
  4. **Dual-Context Inspector:**
     - Hỗ trợ 2 chế độ hiển thị:
       - `InspectorMode::Entity`: Khi chọn Entity trên Scene Hierarchy $\rightarrow$ Hiển thị các Component.
       - `InspectorMode::Asset`: Khi chọn file `.amat` (Material) hoặc file `.axle` trong Content Browser $\rightarrow$ Hiển thị bảng chỉnh sửa thuộc tính của file Asset đó.
* **Tiêu chuẩn nghiệm thu (Acceptance Criteria):**
  - Gắn một Script C++ vào Entity $\rightarrow$ Bảng Properties tự hiện thanh trượt tốc độ xe `Speed`, ô tích `EnableTurbo`.
  - Có thể kéo trượt giá trị và game nhận giá trị đó ngay lập tức khi Play.

---

### 📍 Phase 4: Hoàn thiện Content Browser & Hệ thống Tài nguyên (.meta & File Creation)
* **Mục tiêu cốt lõi:** Quản lý toàn bộ tệp tin trên ổ cứng (`Assets/`), cấp UUID cố định cho tài nguyên qua file `.meta`, và cung cấp menu chuột phải sinh file mới.
* **Các công việc kỹ thuật chi tiết:**
  1. **Hệ thống Asset UUID & `.meta` Pipeline:**
     - Khi import asset (`.obj`, `.png`, `.axle`), tự động tạo file đi kèm `.meta` lưu UUID cố định.
     - Khi đổi tên hoặc di chuyển file, liên kết trong Scene vẫn được giữ nguyên nhờ tra cứu qua UUID.
  2. **Menu chuột phải `Create` sinh file vật lý mới:**
     - `New Folder`: Tạo thư mục trên đĩa.
     - `Scene (.axlescene)`: Tạo file màn chơi rỗng.
     - `Material Asset (.amat / .axlemat)`: Tạo file cấu hình vật liệu độc lập (Albedo, Roughness, Metallic, Texture UUIDs).
     - `C++ Script (.hpp)`: Nhập tên Class $\rightarrow$ Sinh file boilerplate C++ chuẩn kế thừa `ScriptableEntity` có sẵn `OnCreate`, `OnUpdate`, `RegisterFields`.
     - `Physics Material (.physmat)`: Tạo file lưu thông số ma sát và độ nảy.
  3. **Asset Compilation (`Compile Assets - F9`):**
     - Quét và re-bake asset thô sang định dạng nhị phân `.axlebin` tối ưu hóa thời gian nạp.
* **Tiêu chuẩn nghiệm thu (Acceptance Criteria):**
  - Chuột phải trong Content Browser $\rightarrow$ Chọn `Create Material` $\rightarrow$ Sinh file `.amat`.
  - Click vào file `.amat` $\rightarrow$ Bảng Properties hiện bảng chỉnh màu vật liệu.

---

### 📍 Phase 5: Kết nối Toàn bộ Luồng Drag & Drop & Quản lý Trạng thái Play / Stop Mode
* **Mục tiêu cốt lõi:** Kết nối 3 bảng bằng chuẩn Payload ImGui và thiết lập hệ thống Clone Scene an toàn tuyệt đối khi chạy thử nghiệm game.
* **Các công việc kỹ thuật chi tiết:**
  1. **Kết nối 3 Luồng Drag & Drop chuẩn:**
     - **Luồng 1 (`"CONTENT_BROWSER_ITEM"` $\rightarrow$ Viewport / Hierarchy):** Thả file 3D $\rightarrow$ Tự động sinh Entity tại tọa độ raycast mặt sàn hoặc làm con của Entity đích.
     - **Luồng 2 (`"CONTENT_BROWSER_ITEM"` $\rightarrow$ Properties):** Thả model/texture/material/script vào ô nhận slot tương ứng trong Inspector.
     - **Luồng 3 (`"SCENE_HIERARCHY_ENTITY"` $\rightarrow$ Properties / Content Browser):**
       - Thả Entity vào ô `TargetEntity` của C++ Script bên Properties $\rightarrow$ Gán UUID của Entity đích.
       - Thả Entity từ Hierarchy xuống Content Browser $\rightarrow$ Đóng gói toàn bộ nhánh Entity đó thành file `.prefab` lưu xuống đĩa.
  2. **Hệ thống Snapshot & Deep Copy Scene (Edit vs Play Simulation):**
     - Khi nhấn `Play (F5)`:
       - Tạo bản sao `m_RuntimeScene = Scene::Copy(m_EditorScene)`.
       - Dùng `std::unordered_map<UUID, EntityID>` để ánh xạ lại toàn bộ quan hệ Cha-Con và biến tham chiếu Script.
       - Khởi chạy Jolt Physics và gọi `OnCreate()` của các C++ Script trên `m_RuntimeScene`.
     - Khi nhấn `Stop (F7 / Esc)`:
       - Gọi `OnDestroy()`, hủy hoàn toàn `m_RuntimeScene`.
       - Khôi phục lại `m_EditorScene` ban đầu $\rightarrow$ Tất cả vị trí, vận tốc, góc xoay của xe và vật thể quay về chính xác trạng thái trước khi Play.
* **Tiêu chuẩn nghiệm thu (Acceptance Criteria):**
  - Chạy thử xe, đâm sập hàng rào, xe lộn nhào $\rightarrow$ Bấm Stop $\rightarrow$ Cả xe và hàng rào trở lại vị trí ban đầu nguyên vẹn.
  - Kéo thả mượt mà trên cả 3 luồng không có bất kỳ lỗi payload nào.

---

## 7. LƯU Ý KỸ THUẬT BẮT BUỘC KHI IMPLEMENT C++ (CRITICAL CONSTRAINTS)

1. **World Position Stays (Phase 1 & 2):** Khi gán Parent hoặc Unparent cho một Entity, bắt buộc phải nhân nghịch đảo ma trận `Local = Inverse(ParentWorld) * ChildWorld` và Decompose lại Position/Rotation/Scale cục bộ để vật thể Con KHÔNG bị nhảy vị trí trong không gian 3D. Bắt buộc kiểm tra chống vòng lặp (Cycle Check: không cho phép gán Cha làm con của chính Con/Cháu nó).
2. **Deferred & Recursive Lifecycle (Phase 2):**
   * Xóa Entity phải được đưa vào hàng đợi xử lý ở cuối frame (tránh hỏng ImGui loop/iterator) và phải xóa đệ quy toàn bộ các Entity Con bên dưới.
   * Duplicate (Ctrl+D) một Entity Cha phải nhân bản đệ quy toàn bộ cây Entity Con của nó kèm UUID mới.
3. **C++ Script Reflection Macro (Phase 3):** Vì C++ không có runtime reflection tự động cho biến trần, hãy xây dựng hệ thống đăng ký biến bằng Macro (ví dụ `AXLE_SCRIPT_FIELDS(...)` hoặc hàm `OnRegisterFields()`) trong `ScriptableEntity` để Properties Panel tự động đọc danh sách biến, tự vẽ ImGui Slider/Checkbox/EntityRef và tự động Serialize/Clone khi bấm Play.
4. **Dual-Context Properties Panel (Phase 3 & 4):** Properties Panel phải hỗ trợ 2 chế độ (`InspectorMode::Entity` khi chọn Entity trên Scene Hierarchy, và `InspectorMode::Asset` khi chọn file `.amat` dưới Content Browser).
5. **UUID Remapping khi Scene::Clone() (Phase 5):** Khi copy từ `m_EditorScene` sang `m_RuntimeScene` lúc bấm Play (F5), phải dùng `unordered_map<UUID, EntityID>` để ánh xạ lại toàn bộ liên kết trong `RelationshipComponent` và các biến tham chiếu trong Script.