# MEngine 代码审查（2026-10-29，基于 `0a9cf0c`）

范围：`source/engine`、`source/viewer`、`source/shaders`、`cmake`、`programs/IblBaker`。行号以本次注释精简后的代码为准。标 ✔ 的条目已人工复核代码确认。

优先级：**P0** 是确定的缺陷，应尽快修；**P1** 必须在阶段 3（多线程）之前解决；**P2** 应在对应的后续阶段开始之前解决；**P3** 是整理类工作，顺手做。

## 1. 确定的缺陷（P0）

| # | 位置 | 问题 | 建议 |
| --- | --- | --- | --- |
| 1 ✔ | `shaders/Pbr.slang:66-91` | 双面材质背面的法线贴图 TBN 符号错误。传入的 N 已取反；有切线的分支得到 (-T, +B, -N)，用导数构建的分支得到 (T, -B, -N)，正确结果应为 (-T, -B, -N)。影响带法线贴图的双面植被背面（Sponza、Bistro）。 | 用未取反的法线构建 TBN，扰动后整体取反；或两个分支都只取反 T |
| 2 ✔ | `shaders/common/Brdf.slang:DGgx`、`Pbr.slang:143` | roughness 没有下限。roughness = 0 且 NoH = 1 时 `DGgx` 为 0/0 = NaN；粗糙度很小时 D 会超过 RGBA16F 上限变成 inf。MetalRoughSpheres 有 roughness 0 的球。 | 解析光源路径钳制 `max(roughness, 0.045)`（IBL 不受影响） |
| 3 ✔ | `rhi/GpuDevice.cpp:959-968` | 上传批次不是 RAII：`CreateStagingBuffer` 抛异常后 `m_uploadDepth` 永久加 1，之后所有上传都不再提交且不报错；`CreateBuffer` 还会泄漏已创建的 VkBuffer。 | 把 `GltfLoader.cpp` 里的 `UploadBatchScope` 提升为 RHI 公共类型，内部也用它；`CreateBuffer` 用 scope guard 回收 |
| 4 | `rhi/GpuDevice.cpp:~1495-1507`、`ShaderReflection.cpp:142` | 描述符写入固定 `descriptorCount = 1`、`dstArrayElement = 0`，但 layout 和反射都允许 `count > 1`：数组中除第 0 个之外的元素从不写入。 | `DescriptorResource` 加 `arrayElement`；或在 count > 1 时显式报错 |
| 5 | `rhi/GpuDevice.cpp:~1516` | 纹理无效时总是回退到 2D dummy，cube 绑定会因此触发校验错误；`m_dummyCubeTexture` 已创建，但这里没有用到。 | 按绑定的维度选择对应的 dummy |
| 6 | `rhi/GpuDevice.cpp:~2336-2351` | 动态 uniform 环在帧中途耗尽时抛异常，此时 `m_frameActive` 仍为 true，下一次 `BeginFrame` 断言失败，帧状态无法恢复。 | 耗尽时返回失败并记录，下一帧扩容 |
| 7 | `viewer/ViewerApp.cpp:209-265` | 忽略 `Renderer::LoadScene` 的返回值：加载失败时 `m_currentScene` 照样切换，pipeline cache 照样保存，下拉框显示的是失败的场景。 | 按返回值处理失败分支 |
| 8 | `render/GltfLoader.cpp:877-902`、`DdsFile.cpp:538,613` | 工作线程里的解码异常在 `future.get()` 处重新抛出，导致整个场景加载失败，违背“纹理失败只告警”的约定；DDS 的 `mipMapCount` / `arraySize` 没有上限，一个畸形文件就可能 `bad_alloc`。 | lambda 内捕获异常写入 `prepared.error`；mip 数钳制到 `log2(max(w,h)) + 1` |
| 9 | `render/Environment.cpp:176`、`GltfLoader.cpp:929,1693` | `catch (const std::exception&)` 会把 `CheckVk` 抛出的 OOM、device lost 也当成资源损坏，降级处理。 | RHI 抛专用的 `VulkanError`，资源路径只捕获资源类错误 |
| 10 | `render/graph/FrameGraph.cpp:453,576` | 导入纹理被销毁时，只有第一个导入它的 pass 被标为无效，其他读取者仍被调度并拿到失效句柄。 | 把资源本身标为不可用，依赖它的 pass 一律跳过 |
| 11 | `render/graph/FrameGraph.cpp:778,1214` | 合并组成员数没有上限，超过 64 时 `1ull << member` 是未定义行为（只有一个 assert）。 | 合并时限制组大小，或改用动态位集 |
| 12 | `rhi/CommandBuffer.cpp:~385` | `FillBuffer` 只检查了 `offset`，没有检查 `offset + size <= size`，与错误信息不符。 | 补上这项检查 |
| 13 | `rhi/GpuDevice.cpp:~1601` | render pass 缓存只用 64 位哈希、不比较内容，哈希冲突时会静默返回错误的 `VkRenderPass`。 | 命中后比较 `RenderPassOutput` |
| 14 | `rhi/GpuEnums.hpp:~57` | `kResourceStateGenericRead` 没有使用，而且按 `ToVkImageLayout` 的优先级会被映射成 `TRANSFER_SRC` 布局，是个陷阱。 | 删除 |
| 15 | `shaders/common/Bindings.slang`、`DebugLine.slang` | DebugLine 包含 Bindings 后，同一模块里有两个 push_constant 块，能用完全依赖 `-O2` 删掉未用的那个。 | 拆分 Bindings（见 P2-8） |
| 16 | `cmake/StageAssets.cmake:90-100` | 资产镜像是 MEngine 的 POST_BUILD 步骤，只改 `.slang` 或资产时 exe 旁的 `assets/` 可能不会更新（Ninja 下必然如此，VS 在 IDE 里也可能）。需要验证。 | 改为独立的 `MEngineMirrorAssets` ALL 目标 |
| 17 | `android/app/build.gradle:6` | 所有 variant 共用一个 `generated/mengine_assets`，Debug 和 Release 的 CMake 会互相覆盖、互相清理。 | 按 variant 分目录 |

## 2. 阶段 3（多线程）之前必须解决（P1）

当前设计的隐含前提是“单线程、单命令缓冲、录制顺序即执行顺序”。阶段 3 会同时打破这三点。

1. **资源注册表没有同步**：`core/ResourcePool.hpp:52,84` 的 `std::vector<bool>` 位压缩，并发读写不同下标也会数据竞争；`Release` 立刻复用槽位，还持有裸指针的地方（如 `CommandBuffer` 的 `m_currentPipeline`）会读到别的资源。`GpuDevice` 的 `m_pendingDestructions`、render pass 缓存、descriptor pool、pipeline cache、`m_pipelineStats` 都没有锁；上传和 `EndFrame` 直接 `vkQueueSubmit`。→ 写明线程契约；池加锁，或改为“任意线程拿 handle、Vulkan 对象创建加锁”；所有提交走一个加锁的提交器；槽位回收进延迟销毁队列。
2. **全局纹理状态跟踪**：`GpuResources.hpp:350` 的 `Texture::state` 在录制时改写（`CommandBuffer.cpp:142,147,465`；`FrameGraph.cpp` 的 `RecordBarriers` 同理），并行录制下录制顺序不等于执行顺序。→ 由 Frame Graph 在编译期或每帧串行准备阶段生成全部 barrier，录制线程只照计划执行；或每个命令缓冲维护本地状态，提交时合并。
3. **没有 secondary command buffer**：没有 `BeginSecondary` / `ExecuteCommands`；secondary 里 `m_insidePass` 恒为 false，Draw 的 assert 必然触发；`BindPass` 隐式设置的视口不会被继承；每帧槽只有一个命令池（`GpuDevice.hpp:287,292`）。另外，合并组的 subpass 一旦用 `SECONDARY_COMMAND_BUFFERS` 开始，primary 里只能执行 `vkCmdExecuteCommands`，组内逐节点的 timestamp 和 clear 设置都要搬家（`FrameGraph.cpp:1234,1252`）。→ 每线程每帧槽一个命令池；`BindPass(handle, contents)`；Frame Graph 的 Execute 拆成“串行准备 → 并行录制 → 串行组装”三段。
4. **帧接口依赖隐式时序**：`GetCommandBuffer`、`DynamicAllocate`、`GetFramebuffer` 只在 `BeginFrame` 和 `EndFrame` 之间有效；`ConsumeSwapchainResized` 读一次就清掉；`AccessXxx` 返回池内部的可写裸指针。→ `BeginFrame` 返回 `FrameContext`（命令缓冲工厂、图像索引、临时分配器）；`Access` 返回 const，状态修改走设备接口。
5. **动态 uniform 环是非原子的线性分配器**（`GpuDevice.cpp:~2336`）。→ 按线程分块（atomic `fetch_add`），用途泛化到 storage 和 indirect。
6. **上传同步阻塞，并且和 `Create*` 绑在一起**：每批都在 CPU 上等 fence，每个资源单独建 staging，无初始数据的普通纹理仅为一次布局转换也要单独提交（`GpuDevice.cpp:825-920,1180-1316`）。→ 拆出 `UploadContext`：常驻映射的 staging ring，返回 ticket（fence 或帧号），提供就绪查询；`Create*` 只建对象。
7. **Pass 的 Execute 不是只读的**：`ForwardPbrPass::Pipeline()` 和 `ShadowPass` 在 Execute 中懒建管线（会改 `m_pipelines` 并同步调用 `vkCreateGraphicsPipelines`）；Execute 里还写统计和 `m_dirty` / `m_rendered`。→ 变体必须在 `WarmPipelines` / `OnCompiled` 建好，录制时缺失就报错跳过；状态更新挪到串行的 `OnExecuted`。
8. **glTF 加载原地改写 `Scene&`，CPU 解析与 GPU 创建交织**（`GltfLoader.cpp:502,1641-1709`）：读 accessor 时就 `CreateBuffer`；图片 I/O 在主线程；`LoadScene` 和 `LoadGltfScene` 都 `Destroy` 了一次。→ 拆成 `ParseGltf(path) -> SceneData`（纯 CPU，可在工作线程运行、可单测）和 `UploadScene(device, SceneData) -> Scene`，Renderer 在帧边界交换；`m_sceneVersion` 并入 Scene。
9. **查看器的场景切换是一条同步链**（`ViewerApp.cpp:209-265`）：相机复位、cache 保存、灯光复位都假设加载在一次调用内完成。→ 改为 `RequestScene` 加状态（Loading / Ready / Failed），收尾工作放到 `OnSceneReady`。
10. **GpuProfiler 的 scope 栈全局唯一**（`GpuProfiler.hpp:25,63-65`），多个命令缓冲同时 Push/Pop 会打乱嵌套和 query 编号。→ 每个命令缓冲用原子操作申请一段 query 区间。
11. **FileSystem**（`core/FileSystem.cpp:24-35`）：Android 上 SDL 的 `asset_manager` 是首次打开时惰性创建的，没有加锁，目前安全只是因为第一次读取发生在主线程；`ReadFileBytes` 会把 SDL 缓冲再复制一遍。→ 在 `Application::Initialize` 里预热一次并写明线程约束；直接移交缓冲。
12. **纹理解码每张纹理开一个 `std::async` 线程**（`GltfLoader.cpp:~877`）。→ 换成阶段 3 的线程池。

## 3. 后续阶段之前要扩展（P2）

1. **没有启用特性的通道**（`GpuDevice.cpp:388-439`）：只启用 `VK_KHR_swapchain`，特性只通过 1.0 的 `pEnabledFeatures` 传入，没有 Features2 的 pNext 链，T1 能力全都打不开。→ 新增 `EnabledDeviceFeatures`，按能力构建 pNext 链（`Chain` 工具从匿名命名空间移出）；渲染路径一律查“已启用”。同时把 `m_samplerAnisotropy`、`m_multiDrawIndirect` 等重复的布尔值合并进去（`GpuDevice.hpp:251-268`）。*阶段 4 之前。*
2. **档位不该当功能开关**（`DeviceCapabilities.cpp:205-255`）：T2 要求所有能力同时具备，异步计算如果按档位判断，没有光追的桌面卡也会被挡掉；`Bindless()` 没有检查上限；`drawIndirectCount` 等只看扩展名，其他字段看特性位，语义不一致。→ 档位只用于显示，功能按单项判断。
3. **描述符模型**：layout 不支持 binding flags（PARTIALLY_BOUND / UPDATE_AFTER_BIND / VARIABLE_COUNT）；buffer 描述符的 offset 固定为 0；只有一个固定大小的池，`UpdateDescriptorSet` 先分配新 set 再延迟释放旧 set，峰值翻倍；反射拒绝运行时数组。→ `DescriptorAllocator`：可扩容的池列表、每帧临时池、单独的 bindless 池；增加按元素写入的接口。*阶段 8。*
4. **队列模型写死**：只有一个 `m_queue`，`EndFrame` 写死一个等待和一个信号；能力里只记录“有没有独立队列”，没有队列族索引；barrier 的 queue family 写死为 IGNORED；`ToVkPipelineStages` 总会带上 VS/FS 阶段，这在只有 compute 能力的队列上非法（`CommandBuffer.cpp:448,488`、`VulkanUtils.cpp:180-193`）。→ `Submit(queue, waits, signals)`；barrier 增加所有权转移，或对共享资源用 CONCURRENT；阶段掩码按队列能力过滤。*阶段 9。*
5. **`UnorderedAccess` / `VertexAndConstantBuffer` 映射到 VS|FS|CS**，compute 写后读的 barrier 会等所有 fragment 工作结束，在 tiler 上破坏重叠（`VulkanUtils.cpp:165-187`）。→ 拆分 compute 和 pixel 两种状态，或允许显式指定阶段。
6. **延迟销毁按帧号计数，并且每个对象一个 `std::function`**（`GpuDevice.cpp:~810`）：异步上传和 compute 队列的提交不受帧 fence 覆盖；最小化时不回收。→ 按提交的 fence 或 timeline 值判断，按资源类型分组的删除队列。
7. **Frame Graph 的扩展点**：
   - 没有 History 资源（阶段 4）：读历史正好是“本帧写之前先读”，会被判成声明错误，Load 也会被改成 Clear。→ 双缓冲物理纹理、不参与别名，访问用 `ReadPrevious`，并告知 pass 历史是否有效。
   - 没有瞬时 buffer（阶段 8）：`Resource` 一个结构体同时塞了纹理和 buffer 的字段；`m_bufferStates` 按句柄跨帧累积，从不清理。→ 资源本体改成 variant，增加 `FrameGraphBufferDesc`。
   - 没有队列概念（阶段 9）：`FrameGraphNodeType::Compute` 只表示“在 render pass 外”；别名生命周期用调度位置衡量，跨队列后不成立。
   - `IsActive` 的“跳过保留旧内容”在三种情况下不成立：瞬时纹理被别名复用、合并组的首个节点带 Clear 被跳过、首个写入者被跳过后 Load 已被改成 Clear。→ Compile 时校验，或提供 `SetConditional()` 把相关资源排除出别名。
8. **着色器常量与绑定布局**：`DrawConstants` 已经用满 128 字节，放不下 draw/material 索引和 TAA 需要的 prevModel；`FrameConstants` 把逐视图数据和场景光照（固定 1 个点光源 + 1 个方向光）混在一起；阴影 VS 用的是 `frame.lightViewProjection` 而不是通用视图矩阵；Tonemap 把 set 0 用作私有 set，和“set 0 = 每帧”冲突；`Bindings.slang` 是一个整体。→ 拆成 `ViewConstants`（view、proj、prev、jitter，阴影视图复用）、灯光 StructuredBuffer、逐 draw SSBO（push constant 只传索引）、材质 SSBO；Bindings 拆成 FrameSet / MaterialSet / DrawConstants；固定 0 帧、1 材质、2 pass 私有。*阶段 4、5。*
9. **跨语言常量没有校验**：材质 flag 值、特化常量 id、set 0 的 binding 号（C++ 写字面量 0..4）、顶点 location；布局脚本也不检查嵌套结构。→ 共享头作为唯一来源，或让反射脚本生成这些 `static_assert`。
10. **Scene 数据模型**：每个 bufferView 一个 VkBuffer（Bistro 有几千个），primitive 没有 `firstIndex` / `vertexOffset`；每个材质一个 UBO 和一个 set；`SceneDraw::material` 与 `primitive.material` 恒等，是冗余字段。→ 几何合并成按流划分的大缓冲，材质改成 SSBO 存纹理索引。*阶段 8。*
11. **Swapchain**：usage 只有 `COLOR_ATTACHMENT`，无法 blit、compute 写入或截图；重建不传 `oldSwapchain`；`Init` 分两次调用；表面格式变化时直接抛异常。

## 4. 职责划分与接口设计（P3，按收益排序）

1. **`GpuDevice` 2400 行，职责过多** → 拆成 `VulkanContext`（instance、device、VMA）、`FrameScheduler`、`UploadContext`、`DeletionQueue`、`DescriptorAllocator`、`RenderPassCache`、`PipelineFactory`（cache、反射校验），`GpuDevice` 只保留资源注册表和门面。P1-6、P2-3、P2-6 正好是这几个子模块。
2. **ImGui 和调试 UI 泄漏进渲染层**：`Renderer.hpp` 包含 `ImGuiLayer.hpp`（连带 SDL 事件）；`Render(..., ImGuiLayer*)`；`FrameGraphPass` 带有 `Description` / `DrawInspector(RenderSettings&)`，`FrameGraphContext` 带有 `RenderView`，通用的图层因此依赖渲染器专有类型；`ShadowPass::DrawInspector` 会读 Execute 写入的状态。→ Renderer 提供 overlay 节点注册入口，ImGuiLayer / ImGuiPass 移出 render；inspector 在 viewer 侧按 pass 名注册，pass 只暴露 const 的统计快照。
3. **`Renderer::Render` 什么都做**（`Renderer.cpp:119-150,193-270`）：填 FrameConstants、阴影拟合、剔除、冻结状态机、三类调试线。阴影拟合在 Renderer、重绘判断在 ShadowPass，两者隐式耦合，拟合方式一变重绘判断就悄悄失效。→ 阴影视图的拟合和重绘判断都收进 `ShadowPass`；剔除和冻结抽成 `Visibility`；调试线抽成 `SceneDebugOverlay`。
4. **`RenderSettings` 是大杂烩**：灯光属于场景内容，不是设置；其余混着后处理、剔除和调试开关，又被整体传给每个 pass。→ 灯光进 `Scene` / `SceneLighting`（分簇需要数组）；拆成 `PostSettings`、`CullingSettings`、`DebugSettings`。
5. **Pass 之间大量重复**：`Pipeline(key)`、`DestroyPipelines`、`WarmPipelines`、兼容性检查、`AddStage(code.data(), size*4)`、Shadow 与 Forward 几乎相同的 draw 循环；`ForwardPbrPass::Shutdown` 只把 `m_device` 置空，其他 pass 都是 `*this = {}`。→ `PipelineVariantCache<N>` + `ShaderProgram` + `DrawSceneItems(cmd, scene, indices, keyMask)`。
6. **`ViewerApp` 是 600 行、放在匿名命名空间里的类**：配置、场景切换、相机模式、灯光复位、命中测试、全部面板都在一处；引擎的生命周期规则（初始化顺序、`WaitIdle`、`MarkSwapchainDirty`、后台时写回 cache）也写在这里，再写一个应用就得照抄。→ 引擎层加 `RenderApplication` 承担生命周期；查看器拆成 `SceneSession`、`CameraRig` 和各个面板类。
7. **重复的 UI**：Lighting / Debug 面板与 `PassInspectors.cpp` 的控件写了两遍，已经出现分歧（"Freeze Culling" 对 "Freeze"，"Use Camera Direction" 只有一边有）；Frame Graph 面板与 `FrameGraphViewer` 的开关、生命周期表也重复。→ 公共的 `EditDirectionalLight` / `EditPointLight` 等控件函数；表格并入 FrameGraphViewer。
8. **UI 命中测试靠手工拼窗口矩形**（`ViewerApp.cpp:268-279`），新增窗口忘了加就会让相机误吃输入；矩形有 `float[2]`、`glm::vec2` 两种表示。→ `ImGuiLayer` 在 `EndFrame` 时记录所有窗口和弹窗的矩形，统一提供 `HitTest`。
9. **CommandBuffer 的隐式状态**：graphics 和 compute 共用一个 `m_currentPipeline`，`BindPass` / `EndPass` 都会清空它；`BindDescriptorSet` 必须先绑管线，没法在命令缓冲开头一次绑好全局 set；set 编号写在三处（layout 的 `setIndex`、PipelineCreation 里的位置、Bind 的参数），没有一致性检查；`SetClearColor` 的值跨 pass 残留。→ 按绑定点分别跟踪；set 编号直接取 layout 的 `setIndex`；清屏值作为 `BindPass` 的参数。
10. **错误处理不一致**：抛异常、assert、记日志后跳过、返回 bool、返回 optional + `string*`、返回空 vector、BC7 输入不足时静默输出黑色，混用；Release 下 builder 超限静默丢弃，barrier 超过 8 条会悄悄丢掉同步；校验错误在 Debug 抛异常、Release 只记日志。→ 定一条规则：编程错误 assert + 日志 + 跳过；运行时错误（内存不足、池满、资产损坏）throw 或 `Result<T>`；builder 超限硬失败。
11. **Frame Graph 的 Compile 难以单测**：纯算法（剔除、合并、生命周期、别名）与 `GpuDevice` 绑在一起；`CheckReadsBeforeWrites` 名为 Check，实际会改声明（Load → Clear），外层还要重试。→ 纯函数 `CompileGraph(declarations, caps) -> CompiledGraph`，再由 realize 步骤创建 GPU 对象。
12. **Frame Graph 用字符串标识资源和节点**：`Read` / `ColorAttachment` 只接受名字，拼写错误要到 Compile 才暴露；`SetEnabled` / `IsScheduled` 每帧线性扫描比较字符串；调试快照的 `kind` / `status` 也是字符串，viewer 用 `strcmp` 或按前缀解析。→ Create / Import / AddPass 返回带类型的句柄，状态用枚举。
13. **反射与管线创建耦合**：Release 下每次 `CreatePipeline` 仍会反射 SPIR-V；`ValidatePipelineInterface` 基本复制了一遍 `ReflectShaderState`；TonemapPass 同一组着色器反射两次。→ 反射结果复用或按哈希缓存，非校验构建跳过校验。
14. **着色器 / IBL 约定散落**：mip ↔ roughness 映射和 LUT 轴约定分散在 `Prefilter.cpp:14`、`Brdf.slang`、`Skybox.slang:27`（还内联重写了一遍）；烘焙产物提交在仓库里，没有重新烘焙的构建规则，`--self-test` 也没有接入测试；`ShadowSampling.slang` 隐式依赖 Bindings 里的全局变量，却没有 include 它。→ 新增 `SpecularMipFromRoughness()`；把约定写进 KTX2 的 key/value 并在加载时校验；增加 `MEngineBakeIbl` 目标和 `add_test`。
15. **CMake**：着色器编译、暂存和镜像挂在 viewer 目标上，引擎库自己拿不到着色器；依赖不准确（没有 depfile、不依赖 slangc、改任一 common 文件全部重编）；反射探针把三个 FragmentMain 再编译一遍；tinygltf3 以 PUBLIC 链接。→ `-depfile` + `DEPFILE`；探针复用正式编译产物；MEngineCore 依赖 MEngineShaders；tinygltf3 改为 PRIVATE。
16. **asset 层边界**：DDS 解析在 render、BC7 解码在 asset、mip 选择在 GltfLoader，纯 CPU 的纹理准备离不开 render 层；`MipExtent` 实现了两份。→ DDS 与 mip 选择移进 asset，为在工作线程上做完整的纹理准备铺路。
17. **其他接口小问题**：`Json` 的 `JsonValue` 不持有数据，Document 销毁后悬空，而且不能遍历键，config 的拼写错误会被静默忽略；`Input::IsKeyDown` 直接轮询 SDL，没有边沿接口；相机控制器的 `uiWantsMouse` 只有 Orbit 在用，`Activate(Camera&)` 与 `Deactivate(Window&)` 不对称；`CameraScene::Pose` 与 `CameraConfig` 字段重复；应用标识 `"MEngine"` 在 FileSystem、main、Window 三处各写一份；`OnShutdown` 在初始化失败后也会被调用，但没有写明契约。

## 5. 重复代码

| 内容 | 位置 |
| --- | --- |
| AABB 8 角点变换（三处），空包围盒判断标准不一致（只看 x 或看 xyz） | `Renderer.cpp:127`、`GltfLoader.cpp:1500`、`DebugDraw.cpp:70,109`；`Scene.hpp:88`、`Culling.cpp:39` → 引入 `struct Aabb` |
| FNV-1a | `GpuResources.cpp:9-19`、`PipelineCacheFile.cpp:17-26` → `core/Hash.hpp` |
| VMA 屏蔽警告的 pragma 块 | `VulkanUtils.hpp:7-27`、`VmaUsage.cpp:4-28` → `rhi/VmaInclude.hpp`；`GpuResources.hpp` 改为前置声明 VMA 类型 |
| Uncharted 2 曲线与 sRGB 转换 | `Tonemap.slang:32-49`、`DebugLine.slang:34-58` → `common/Tonemapping.slang` |
| 全屏三角形 VS | `Skybox.slang:11-19`、`Tonemap.slang:20-29` |
| PI 常量（三份，命名不一致） | `Brdf.slang:4`、`UeBrdf.hpp:19`、`Cubemap.cpp:13` |
| 格式名表 | `ViewerApp.cpp:62-80`、`DeviceCapabilities.cpp:~91` |
| 方向向量归一化 UI | `ViewerApp.cpp:370-379`、`PassInspectors.cpp:32-43` |
| 设备扩展枚举、队列族遍历 | `GpuDevice.cpp:89,99,337-386`、`DeviceCapabilities.cpp:28,290-312` |
| `RenderPassCreation` 与 `RenderPassOutput` 字段重复，`colorCount` / `renderTargetCount` 两套计数 | `GpuResources.hpp:208-242` |

## 6. 命名（涉及公开接口，本次未改）

- `RenderSettings::light` → `pointLight`；`FrameConstants` 的 `lightViewProjection`（方向光）和 `lightPositionRadius`（点光源）都用 `light` 前缀，容易混。
- `GetVulkanRenderPass` 会创建对象 → `ObtainVulkanRenderPass`。
- `CommandBuffer::BufferBarrier` / `GlobalBarrier` 遮蔽同名的 `rhi::` 类型，`CommandBuffer::Handle()` 与 `mengine::Handle` 模板同名；`DebugDraw::Frustum()` 遮蔽 `render::Frustum` 类型。
- 取值函数的 `Get` 前缀不统一：`ShadowPass::Texture()`、`DebugDraw::Buffer()`、`Swapchain` 不带，`GpuDevice`、`Renderer` 带。
- `ResourceUsage::Immutable` 实际含义是设备本地（compute 写入的 buffer 也用它）→ `DeviceLocal`，另需补 `Readback`。
- `FrameGraphNodeType` 实际表示“是否在 render pass 内”；`Node::renderPass` 存的是合并组索引。
- `geometricNormal`（`Pbr.slang:154`）其实是插值的顶点法线。
- `CameraController::Update` 的参数在头文件叫 `input`、实现里叫 `context`。
- `TextureData`、`TextureDimension` 等通用名字放在 `mengine::render` 命名空间（DDS 移植代码）→ `render::dds`。
- `HeapUsage` 是运行时统计，不该放在 `DeviceCapabilities` 里；`BeginFrame` 内的 `SDL_Delay` 节流属于应用层决策。

## 7. 死代码

`GpuDevice::ResizeOutputTextures`、`LinkTextureSampler`、`IsVsync`、`GetAllocator`、`GetAbsoluteFrame`、`GetDummyCubeTexture`、`GetDefaultSampler`、`SupportsCompute`、`GetProperties`；`CommandBuffer::IsInsidePass`；`Swapchain::HasSurface`、`ImageCount`；`DescriptorSetLayoutCreation::FindBinding`；`ShaderReflection::localSize`；`kResourceStateGenericRead`；`DebugDraw::Sphere`、`LineCount`、`kSphereSegments`；`Ktx2BytesPerTexel`；`Application::IsSuspended`；`Brdf.slang` 的 `GSmithIbl`（运行时不用）；DDS 的 `forceSRGB`、`alphaMode`、`rowPitch`、`depthPitch`；`CMakeLists.txt:166` 永远为真的 `EXISTS` 判断。保留前需要给出理由，否则删除。

## 8. 性能

- **阴影管线切换次数翻倍**：draw 按 Forward 的完整 key 排序，而 Tangents 位不在阴影的 key 里，阴影 key 呈 0, 2, 4, 6, 0, 2, 4, 6 交替（`ShadowPass.cpp:158-168`）。→ ShadowPass 维护按自己 key 排序的下标列表。
- **set 0 重复绑定**：每次切换管线都重绑 set 0，而兼容的 pipeline layout 绑一次就够；每个 draw 调 3-4 次 `BindVertexBuffer`，每次都查资源池，也不过滤重复绑定。→ 一次 `vkCmdBindVertexBuffers`。
- **重复查询**：`Supports*(format)` 每次调用都执行 `vkGetPhysicalDeviceFormatProperties`；Release 下 `CreatePipeline` 仍会反射 SPIR-V。
- **Frame Graph 与查看器**：`SetEnabled` / `IsScheduled` 每帧做字符串线性扫描；`FrameGraphViewer` 每帧都 `MeasureNode`，`PinConnected` 是 O(引脚 × 连线)；`DeviceCapabilitiesPanel` 打开时每帧重建列表。
- **IBL 资源**：三个立方体贴图是 RGBA16F，alpha 恒为 1，可改用 B10G11R11 让体积减半；`PrefilterEnvMap` 只从 level 0 取样，高粗糙度的 mip 在强光源下会有萤火虫噪点（filtered importance sampling 会偏离讲义原文）。

## 9. 建议的处理顺序

1. **P0 缺陷**：先修 1–3（着色错误和静默失败），其余与相关模块的改动一并处理。
2. **阶段 3 的前置重构**，按依赖顺序：
   1. `GpuDevice` 拆出 `UploadContext`、`DeletionQueue` 和 `DescriptorAllocator`（P1-6、P3-1）。
   2. 资源注册表的线程契约（P1-1）。
   3. `FrameContext`、命令池按线程分配、secondary 接口（P1-3、P1-4）。
   4. Frame Graph 的 Execute 改为三段式，barrier 由图统一生成（P1-2、P1-3）。
   5. Pass 的 Execute 只读（P1-7）。
   6. glTF 加载拆成 Parse 和 Upload 两步（P1-8）。
3. 结构改动的同时，把 ImGui 从渲染层剥离（P3-2），并抽出 `PipelineVariantCache` 和 `Aabb`（P3-5、§5）。
4. 阶段 4 之前：启用特性的通道（P2-1）、View / Lighting 常量拆分与 Bindings 拆分（P2-8）、Frame Graph 的 History 资源（P2-7）。
