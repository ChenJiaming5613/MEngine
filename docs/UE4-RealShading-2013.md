# Real Shading in Unreal Engine 4（SIGGRAPH 2013）公式总结

- 原文：Brian Karis, Epic Games, *Real Shading in Unreal Engine 4*, SIGGRAPH 2013 Course “Physically Based Shading in Theory and Practice”
- 本地副本：`D:\Final\References\2013SiggraphPresentationsNotes-26915738.pdf`（第 1–21 页为 course notes，第 22 页起为演讲幻灯片）
- 用途：MEngine 的 PBR 和 IBL **严格按照本文公式实现**。下文的公式编号和原文一致，页码指 PDF 页码。
- 记号：`n` 法线，`v` 视线方向，`l` 光线方向，`h = normalize(l + v)` 半程向量；点积在使用前都先 `saturate`。

---

## 1. 设计目标（p1–2）

UE4 着色模型的设计原则如下，MEngine 做取舍时也以此为准：

| 目标 | 含义 |
| --- | --- |
| Real-Time Performance | 同屏有很多灯时也要高效 |
| Reduced Complexity | 参数尽量少；IBL 和解析光源可以互换，参数在所有光源类型下行为一致 |
| Intuitive Interface | 参数直观（不直接暴露 IOR 这类物理量） |
| Perceptually Linear | 参数插值的结果接近着色结果的插值（便于材质分层混合） |
| Easy to Master | 美术不需要理解电介质和导体的区别 |
| Robust | 很难做出物理上不合理的材质 |
| Expressive | 一个基础模型覆盖 99% 的真实材质 |
| Flexible | 也能支持非写实风格 |

---

## 2. 着色模型（Shading Model）

### 2.1 漫反射 BRDF：Lambert（p2，式 1）

\[
f_d(\mathbf{l},\mathbf{v}) = \frac{c_{diff}}{\pi}
\]

- `c_diff` 为漫反射反照率。
- 对比过 Burley 漫反射，差异很小，而且很难高效地用在 IBL 和 SH 光照上，所以选用 Lambert。

### 2.2 微表面镜面 BRDF：Cook-Torrance（p3，式 2）

\[
f_s(\mathbf{l},\mathbf{v}) = \frac{D(\mathbf{h})\,F(\mathbf{v},\mathbf{h})\,G(\mathbf{l},\mathbf{v},\mathbf{h})}{4\,(\mathbf{n}\cdot\mathbf{l})\,(\mathbf{n}\cdot\mathbf{v})}
\]

### 2.3 D：GGX / Trowbridge-Reitz（p3，式 3）

采用 Disney 的重参数化 \(\alpha = Roughness^2\)：

\[
D(\mathbf{h}) = \frac{\alpha^2}{\pi\left((\mathbf{n}\cdot\mathbf{h})^2(\alpha^2-1)+1\right)^2}
\]

### 2.4 G：Schlick-Smith（p3，式 4）

\[
G_1(\mathbf{v}) = \frac{\mathbf{n}\cdot\mathbf{v}}{(\mathbf{n}\cdot\mathbf{v})(1-k)+k},\qquad
G(\mathbf{l},\mathbf{v},\mathbf{h}) = G_1(\mathbf{l})\,G_1(\mathbf{v})
\]

k 有**两套取值**，不能混用：

| 场景 | k | 说明 |
| --- | --- | --- |
| 解析光源（点光源等） | \(k = \dfrac{(Roughness+1)^2}{8}\) | 用 Disney 的 remap \((Roughness+1)/2\) 再平方，减轻过亮（hotness） |
| IBL（预滤波和 BRDF LUT） | \(k = \dfrac{\alpha}{2}\) | 原文：“this adjustment is only used for analytic light sources; if applied to image-based lighting, the results at glancing angles will be much too dark.” |

\(k = \alpha/2\) 时，Schlick 在 \(\alpha = 1\) 处和 Smith-GGX 完全一致，在 [0,1] 范围内也很接近（原文 Figure 2）。

### 2.5 F：Schlick + 球面高斯近似（p3，式 5）

\[
F(\mathbf{v},\mathbf{h}) = F_0 + (1-F_0)\,2^{\left(-5.55473(\mathbf{v}\cdot\mathbf{h}) - 6.98316\right)(\mathbf{v}\cdot\mathbf{h})}
\]

- 用球面高斯代替 Schlick 的五次方，计算稍快，视觉上看不出差异。
- `F0` 为法线入射时的镜面反射率。
- 注意：在 IBL 的积分代码（第 3 节）里，原文用的仍是标准 Schlick 形式 `Fc = pow(1 - VoH, 5)`。

---

## 3. 基于图像的光照（Image-Based Lighting）

### 3.1 辐射度积分的重要性采样（p4，式 6）

\[
\int_H L_i(\mathbf{l})\,f(\mathbf{l},\mathbf{v})\cos\theta_l\,d\mathbf{l} \approx \frac{1}{N}\sum_{k=1}^{N}\frac{L_i(\mathbf{l}_k)\,f(\mathbf{l}_k,\mathbf{v})\cos\theta_{l_k}}{p(\mathbf{l}_k,\mathbf{v})}
\]

原文 HLSL（p4–5），GGX 重要性采样：

```hlsl
float3 ImportanceSampleGGX( float2 Xi, float Roughness, float3 N )
{
    float a = Roughness * Roughness;
    float Phi = 2 * PI * Xi.x;
    float CosTheta = sqrt( (1 - Xi.y) / ( 1 + (a*a - 1) * Xi.y ) );
    float SinTheta = sqrt( 1 - CosTheta * CosTheta );
    float3 H;
    H.x = SinTheta * cos( Phi );
    H.y = SinTheta * sin( Phi );
    H.z = CosTheta;
    float3 UpVector = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 TangentX = normalize( cross( UpVector, N ) );
    float3 TangentY = cross( N, TangentX );
    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}
```

参考实现（逐样本求积分的完整 IBL，作为 ground truth）：

```hlsl
float3 SpecularIBL( float3 SpecularColor, float Roughness, float3 N, float3 V )
{
    float3 SpecularLighting = 0;
    const uint NumSamples = 1024;
    for( uint i = 0; i < NumSamples; i++ )
    {
        float2 Xi = Hammersley( i, NumSamples );
        float3 H = ImportanceSampleGGX( Xi, Roughness, N );
        float3 L = 2 * dot( V, H ) * H - V;
        float NoV = saturate( dot( N, V ) );
        float NoL = saturate( dot( N, L ) );
        float NoH = saturate( dot( N, H ) );
        float VoH = saturate( dot( V, H ) );
        if( NoL > 0 )
        {
            float3 SampleColor = EnvMap.SampleLevel( EnvMapSampler, L, 0 ).rgb;
            float G = G_Smith( Roughness, NoV, NoL );
            float Fc = pow( 1 - VoH, 5 );
            float3 F = (1 - Fc) * SpecularColor + Fc;
            // Incident light = SampleColor * NoL
            // Microfacet specular = D*G*F / (4*NoL*NoV)
            // pdf = D * NoH / (4 * VoH)
            SpecularLighting += SampleColor * F * G * VoH / (NoH * NoV);
        }
    }
    return SpecularLighting / NumSamples;
}
```

- 这里的 `G_Smith` 使用 IBL 那套 \(k = \alpha/2\)。
- `Hammersley(i, N)` 生成二维低差异序列 \((i/N,\ \text{RadicalInverse}_2(i))\)。原文没有给出代码，属于标准定义。
- 即使用了重要性采样，样本数也要很多。借助 mip map 可以大幅减少样本，但仍需 > 16 个。UE4 每个像素要混合多张环境图，所以每张图只能采 1 次，这就引出了 split sum。

### 3.2 Split Sum 近似（p5，式 7）

\[
\frac{1}{N}\sum_{k=1}^{N}\frac{L_i(\mathbf{l}_k)f(\mathbf{l}_k,\mathbf{v})\cos\theta_{l_k}}{p(\mathbf{l}_k,\mathbf{v})}
\approx
\left(\frac{1}{N}\sum_{k=1}^{N}L_i(\mathbf{l}_k)\right)
\left(\frac{1}{N}\sum_{k=1}^{N}\frac{f(\mathbf{l}_k,\mathbf{v})\cos\theta_{l_k}}{p(\mathbf{l}_k,\mathbf{v})}\right)
\]

- 第一项是预滤波环境贴图，第二项是环境 BRDF，两项分别预计算。
- 当 \(L_i\) 为常量时这个近似是精确的，对常见环境也相当准确。

### 3.3 预滤波环境贴图（p5–6）

- 对每个粗糙度预计算第一项，存进立方体贴图的各级 mip。
- 用本模型的 GGX 分布做重要性采样卷积。
- **各向同性假设**：\(\mathbf{n} = \mathbf{v} = \mathbf{r}\)。代价是掠射角处没有拉长的反射，这一项是 UE4 IBL 方案中**比 split sum 本身更大的误差来源**。
- 按 \(\cos\theta_{l_k}\)（即 `NoL`）加权效果更好。式 7 为了简洁没有写这个权重。

```hlsl
float3 PrefilterEnvMap( float Roughness, float3 R )
{
    float3 N = R;
    float3 V = R;
    float3 PrefilteredColor = 0;
    const uint NumSamples = 1024;
    for( uint i = 0; i < NumSamples; i++ )
    {
        float2 Xi = Hammersley( i, NumSamples );
        float3 H = ImportanceSampleGGX( Xi, Roughness, N );
        float3 L = 2 * dot( V, H ) * H - V;
        float NoL = saturate( dot( N, L ) );
        if( NoL > 0 )
        {
            PrefilteredColor += EnvMap.SampleLevel( EnvMapSampler, L, 0 ).rgb * NoL;
            TotalWeight += NoL;
        }
    }
    return PrefilteredColor / TotalWeight;
}
```

（原文代码漏了 `float TotalWeight = 0;` 的声明，实现时要补上。）

### 3.4 环境 BRDF（p6，式 8）

- 第二项等价于在纯白环境（\(L_i = 1\)）下对镜面 BRDF 积分。
- 代入 Schlick Fresnel \(F = F_0 + (1-F_0)(1-\mathbf{v}\cdot\mathbf{h})^5\) 后，可以把 \(F_0\) 提到积分外：

\[
\int_H f(\mathbf{l},\mathbf{v})\cos\theta_l\,d\mathbf{l}
= F_0\int_H \frac{f(\mathbf{l},\mathbf{v})}{F(\mathbf{v},\mathbf{h})}\left(1-(1-\mathbf{v}\cdot\mathbf{h})^5\right)\cos\theta_l\,d\mathbf{l}
+ \int_H \frac{f(\mathbf{l},\mathbf{v})}{F(\mathbf{v},\mathbf{h})}(1-\mathbf{v}\cdot\mathbf{h})^5\cos\theta_l\,d\mathbf{l}
\]

- 输入为 `Roughness` 和 \(\cos\theta_v\)（NoV），输出为 F0 的 scale（A）和 bias（B），都在 [0,1] 范围内，存成 2D LUT。
- **格式用 R16G16**，原文脚注 2：“we found precision to be important”。

```hlsl
float2 IntegrateBRDF( float Roughness, float NoV )
{
    float3 V;
    V.x = sqrt( 1.0f - NoV * NoV ); // sin
    V.y = 0;
    V.z = NoV;                      // cos
    float A = 0;
    float B = 0;
    const uint NumSamples = 1024;
    for( uint i = 0; i < NumSamples; i++ )
    {
        float2 Xi = Hammersley( i, NumSamples );
        float3 H = ImportanceSampleGGX( Xi, Roughness, N );
        float3 L = 2 * dot( V, H ) * H - V;
        float NoL = saturate( L.z );
        float NoH = saturate( H.z );
        float VoH = saturate( dot( V, H ) );
        if( NoL > 0 )
        {
            float G = G_Smith( Roughness, NoV, NoL );
            float G_Vis = G * VoH / (NoH * NoV);
            float Fc = pow( 1 - VoH, 5 );
            A += (1 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2( A, B ) / NumSamples;
}
```

（这里的切线空间里 `N = float3(0, 0, 1)`，原文代码没有显式声明。）

### 3.5 运行时组合（p7）

```hlsl
float3 ApproximateSpecularIBL( float3 SpecularColor, float Roughness, float3 N, float3 V )
{
    float NoV = saturate( dot( N, V ) );
    float3 R = 2 * dot( V, N ) * N - V;
    float3 PrefilteredColor = PrefilterEnvMap( Roughness, R );   // runtime: sample prefiltered cube mip
    float2 EnvBRDF = IntegrateBRDF( Roughness, NoV );            // runtime: sample 2D LUT
    return PrefilteredColor * ( SpecularColor * EnvBRDF.x + EnvBRDF.y );
}
```

原文 Figure 4/5 的对比结论：在参考结果、仅 split sum、split sum + \(n=v\) 三者中，径向对称假设引入的误差最大，但整体仍和参考结果很接近。

---

## 4. 材质模型（Material Model，p9–10）

| 参数 | 说明 |
| --- | --- |
| BaseColor | 单一颜色，概念简单 |
| Metallic | 不需要理解电介质和导体的反射率；**应当视为二值（0 或 1）**，中间值交给材质分层处理 |
| Roughness | 含义明确（gloss 总需要额外解释） |
| Cavity | 小尺度遮蔽，比如只存在于法线贴图里的几何细节（地板缝、衣服缝线） |

- **去掉了 Specular 参数**：名字容易误导，默认值 0.5（对应 4% 反射率）经常被误设成 1。非金属的 IOR 影响不大，**非金属 F0 固定为 0.04**。
- 没有纳入基础模型、作为特例处理的 Disney 参数：Subsurface、Anisotropy、Clearcoat、Sheen。
- 由此推导的标准 metallic 工作流参数映射如下（原文没有逐行写出，是业界通用写法，和 UE4 实现一致）：

\[
F_0 = \text{lerp}(0.04,\ BaseColor,\ Metallic),\qquad c_{diff} = BaseColor\,(1 - Metallic)
\]

- 美术反馈“Roughness 感觉是反的”：亮的 texel 代表更粗糙，高光反而更弱。
- 材质分层（Material Layering，p10–12）属于工具链内容，MEngine 不涉及。

---

## 5. 光照模型（Lighting Model）

### 5.1 窗口化的平方反比衰减（p12，式 9）

- 采用物理正确的平方反比衰减，亮度单位用 lumens。
- 为了限制影响范围，对衰减函数做窗口化处理：大部分影响区域基本不变，同时平滑过渡到 0。这样调整半径不会改变灯光的有效亮度。

\[
falloff = \frac{\text{saturate}\left(1-(distance/lightRadius)^4\right)^2}{distance^2+1}
\]

- 分母里的 `+1` 防止靠近光源时数值爆炸，必要时可以作为美术参数暴露。
- 原文评价：这是“likely the largest bang for buck takeaway”。

### 5.2 区域光（p13–18）

要求：
1. 漫反射和镜面能量一致。
2. 立体角趋于 0 时退化为点光源模型。
3. 足够快，可以到处使用。否则美术会通过调高 roughness 来补偿，造成着色和光照耦合。

| 方案 | 结论 |
| --- | --- |
| Billboard Reflections | 平面预滤波，立体角有限，光线不与平面相交时没有数据，`l` 未知 |
| Cone Intersection | 太贵，或者要求分布径向对称（丢失拉伸高光） |
| Specular D Modification（式 10） | 光滑材质在大面积光下会显得粗糙，不适合 GGX |
| **Representative Point + 能量守恒** | 满足全部要求，最终采用 |

**Specular D Modification**（式 10，最终没有采用，仅作参考）：

\[
\alpha' = \text{saturate}\left(\alpha + \frac{sourceRadius}{3\,distance}\right)
\]

**球形光**（式 11–14）：
- 只要球体在地平线之上，球形光的辐照度就等于点光源，所以只需要处理镜面部分。
- 用“到反射射线 r 距离最近的点”来近似“和 r 夹角最小的点”：

\[
\begin{aligned}
centerToRay &= \mathbf{L} - (\mathbf{L}\cdot\mathbf{r})\,\mathbf{r}\\
closestPoint &= \mathbf{L} + centerToRay\cdot\text{saturate}\left(\frac{sourceRadius}{|centerToRay|}\right)\\
\mathbf{l} &= \|closestPoint\|
\end{aligned}
\]

- `L` 是着色点到光源中心的向量。
- 移动光源原点相当于加宽了镜面分布，会多出能量。用归一化因子之比来补偿：

\[
I_{point} = \frac{p+2}{2\pi}\cos^p\phi_r \quad(12)
\]

\[
I_{sphere} = \begin{cases}\frac{p+2}{2\pi} & \phi_r < \phi_s\\[2pt] \frac{p+2}{2\pi}\cos^p(\phi_r-\phi_s) & \phi_r \ge \phi_s\end{cases} \quad(13)
\]

\[
SphereNormalization = \left(\frac{\alpha}{\alpha'}\right)^2 \quad(14)
\]

其中 \(\alpha'\) 由式 10 计算；GGX 的归一化因子为 \(1/(\pi\alpha^2)\)。

**管状光 / 线光源**（式 15–20）：

线段的辐照度（线段在地平线之上时），\(\mathbf{L}_0, \mathbf{L}_1\) 为着色点到两个端点的向量：

\[
\int_{L_0}^{L_1}\frac{\mathbf{n}\cdot\mathbf{L}}{|\mathbf{L}|^3}\,d\mathbf{L}
= \frac{\frac{\mathbf{n}\cdot\mathbf{L}_0}{|\mathbf{L}_0|}+\frac{\mathbf{n}\cdot\mathbf{L}_1}{|\mathbf{L}_1|}}{|\mathbf{L}_0||\mathbf{L}_1|+(\mathbf{L}_0\cdot\mathbf{L}_1)} \quad(15)
\]

修正版（避免负值和除零，长度为 0 时和点光源衰减一致）：

\[
irradiance = \frac{2\cdot\text{saturate}\left(\frac{\mathbf{n}\cdot\mathbf{L}_0}{2|\mathbf{L}_0|}+\frac{\mathbf{n}\cdot\mathbf{L}_1}{2|\mathbf{L}_1|}\right)}{|\mathbf{L}_0||\mathbf{L}_1|+(\mathbf{L}_0\cdot\mathbf{L}_1)+2} \quad(16)
\]

镜面代表点：

\[
\mathbf{L}_d = \mathbf{L}_1-\mathbf{L}_0,\qquad \mathbf{l} = \|\mathbf{L}_0+\text{saturate}(t)\,\mathbf{L}_d\| \quad(17)
\]

\[
t_{Picott} = \frac{(\mathbf{L}_0\cdot\mathbf{L}_d)(\mathbf{r}\cdot\mathbf{L}_0)-(\mathbf{L}_0\cdot\mathbf{L}_0)(\mathbf{r}\cdot\mathbf{L}_d)}{(\mathbf{L}_0\cdot\mathbf{L}_d)(\mathbf{r}\cdot\mathbf{L}_d)-(\mathbf{L}_d\cdot\mathbf{L}_d)(\mathbf{r}\cdot\mathbf{L}_0)} \quad(18)
\]

\[
t_{shortest} = \frac{(\mathbf{r}\cdot\mathbf{L}_0)(\mathbf{r}\cdot\mathbf{L}_d)-(\mathbf{L}_0\cdot\mathbf{L}_d)}{|\mathbf{L}_d|^2-(\mathbf{r}\cdot\mathbf{L}_d)^2} \quad(19，实际采用，更便宜)
\]

- 式 18 和式 19 都把 r 当作直线而不是射线，反射方向背离线段时，代表点会在两个端点之间突跳。原文选择接受这个瑕疵。
- 能量守恒：分布只在一维上被加宽，按各向异性 GGX 的归一化因子 \(1/(\pi\alpha_x\alpha_y)\) 推导：

\[
LineNormalization = \frac{\alpha}{\alpha'} \quad(20)
\]

- 线段 + 球体两种操作可以叠加，近似两种形状的卷积，用来模拟胶囊形的管状光。

---

## 6. MEngine 的落地约定

### 6.1 在本次范围内（chapter1 目标：Sponza PBR + IBL）

| 项 | 原文 | MEngine 位置 |
| --- | --- | --- |
| Lambert 漫反射 | 式 1 | `shaders/common/Brdf.slang` |
| Cook-Torrance 组合 | 式 2 | `Brdf.slang` |
| GGX D，\(\alpha = R^2\) | 式 3 | `DGgx`（Slang 和 C++ 同名） |
| Schlick-Smith G，解析光 \(k=(R+1)^2/8\) | 式 4 | `GSmithAnalytic` |
| Schlick-Smith G，IBL \(k=\alpha/2\) | 式 4 + p3 注释 | `GSmithIbl`（IblBaker 的 `UeBrdf.hpp`） |
| 球面高斯 Fresnel | 式 5 | `FSchlickSg`（只用于解析光） |
| Hammersley + `ImportanceSampleGGX` | p4 | `Hammersley` / `ImportanceSampleGgx`（IblBaker 的 `UeBrdf.hpp`） |
| `SpecularIBL`（逐样本参考积分） | p4–5 | `SpecularIbl`（`UeBrdf.hpp`，只用于 `--self-test`） |
| `PrefilterEnvMap`（N=V=R，NoL 加权，从 mip0 采样，1024 样本） | p5–6 | `PrefilterEnvMap`（`UeBrdf.hpp`），由 `Prefilter.cpp` 烘焙 → `specular.ktx2` |
| `IntegrateBRDF`（R16G16） | 式 8，p6 | `IntegrateBrdf`（`UeBrdf.hpp`），由 `BrdfLut.cpp` 烘焙 → `brdf_lut.ktx2` |
| `ApproximateSpecularIBL` | p7 | `ApproximateSpecularIbl`（`Brdf.slang`），在 `Pbr.slang` 中调用 |
| 非金属 F0 = 0.04，Metallic 工作流 | p9 | `SpecularColorFromMetallic` / `DiffuseColorFromMetallic`（`Brdf.slang`） |
| 窗口化平方反比衰减 | 式 9 | `LightFalloff`，`lightRadius` 可在 ImGui 调节 |
| 方向光（式 1–5，无衰减） | 式 1–5 | `EvaluateDirectLight`（`Pbr.slang`），与点光源共用；radiance = color × intensity × 阴影 |

IblBaker 中原文 HLSL 的 C++ 移植保持逐行对应（变量名、循环结构、`saturate` 位置都不变），只把函数名改为项目的大小写风格，并在注释里注明原文出处。

### 6.2 原文没有覆盖、由 MEngine 补充的部分（代码中注明）

- **漫反射 IBL**：原文只讲了镜面 IBL。MEngine 把式 6 应用于式 1：Lambert 项 \(c_{diff}/\pi\) 与 \(\mathbf{l}\) 无关，可以提到积分外，剩下的 \(\frac{1}{\pi}\int_H L_i\cos\theta_l\,d\mathbf{l} = E(\mathbf{n})/\pi\) 用余弦加权的 Hammersley 样本离线估计（pdf \(\cos\theta/\pi\) 与被积函数中的 \(\cos\theta/\pi\) 相消，结果就是 \(\frac{1}{N}\sum L_i(\mathbf{l}_k)\)）。结果存进 `irradiance.ktx2`，运行时计算 \(c_{diff}\cdot irradiance(\mathbf{n})\)。实现见 `Irradiance.cpp` 的 `IrradianceOverPi`。
- **Mip 和粗糙度的映射**：原文没有给出。MEngine 采用线性映射 `Roughness = mip / (mipCount - 1)`，运行时 `textureLod(R, Roughness * (mipCount - 1))`。mip0 不做特殊处理：Roughness 为 0 时 GGX 样本全部落在镜面方向，结果等于源贴图。
- **环境贴图输入**：
  - 等距柱状投影 `.hdr`：用双线性采样转换为立方体贴图。
  - KTX 1.1 立方体贴图（RGBA16F/32F，例如 Vulkan-glTF-PBR 的 `papermill.ktx`）：直接取 mip0，尺寸不同时双线性重采样。
  - 面的顺序和方向遵循 Vulkan 规范的 Cube Map Face Selection。
  - `--flip-y` 把输入沿 y 镜像，即 \(L'(x,y,z) = L(x,-y,z)\)，属于精确的 texel 重排。Vulkan-glTF-PBR 的世界坐标是 glTF 坐标取反 y（`pbr.vert` 中 `locPos.y = -locPos.y`，`material_pbr.frag` 中 `n.y *= -1`），它的环境贴图因此“天空在 -Y”，在 MEngine 的 Y 朝上世界里要做这次镜像。
- **输出格式**：specular、irradiance 为 RGBA16F，BRDF LUT 为 RG16F（满足原文脚注 2 对精度的要求），都存成未压缩的 KTX2。LUT 的 texel \((x, y)\) 对应 `NoV = (x + 0.5) / size`、`Roughness = (y + 0.5) / size`。
- **并行**：按“面 × 行”分发到 `std::thread`，每个 texel 的计算彼此独立，输出与线程数无关。
- **AO**：glTF 的 occlusion 贴图只作用于间接光（IBL），相当于原文 Cavity 的小尺度遮蔽用途；直接光不乘 AO。
- **方向光阴影**：原文没有涉及阴影。MEngine 为方向光渲染一张阴影贴图：
  - 2048² 的 D16 深度图，光空间正交投影贴合场景包围盒（外扩 2 个 texel），和相机投影一样翻转 Y，因此沿用前向 pass 的绕序和剔除规则。
  - alpha MASK 材质在阴影 pass 中按 `alphaCutoff` 镂空。
  - 去痤疮：投射端用管线静态深度偏移（常量 1.25、斜率 1.75），接收端沿几何法线偏移 `Normal Offset × 阴影 texel 世界尺寸`（默认 1.5 texel）。
  - 软化：3×3 PCF；设备支持 D16 线性过滤时，比较采样器使用 LINEAR，每个 tap 本身就是硬件 2×2 PCF。
  - 只在光方向、场景或阴影开关变化时重绘；只作用于方向光的直接光项，点光源不投射阴影。
- **Specular-Glossiness 材质**（`KHR_materials_pbrSpecularGlossiness`，Bistro 的大多数材质）：原文只定义了 Metallic 工作流。MEngine 把它映射到同一套着色模型：
  - \(F_0 = specular\)（扩展本身就是 F0 颜色），\(Roughness = 1 - glossiness\)，再进入式 3–5 和 split sum IBL；
  - \(c_{diff} = diffuse \cdot (1 - \max(F_0.r, F_0.g, F_0.b))\)，按扩展参考实现做能量守恒；
  - specular 与 diffuse 贴图按 sRGB 采样，glossiness 在 A 通道保持线性。
- **透射材质**（`KHR_materials_transmission`）：不实现折射，这类图元不绘制，也不投射阴影。
- **色调映射**：曝光后用 Hable 的 Uncharted 2 filmic 曲线，按白点 11.2 归一化，默认曝光 4.5。常量和默认值与 Vulkan-glTF-PBR 相同，最后按 sRGB 编码输出。天空盒与场景共用这一步，没有单独的曝光。

### 6.3 本次不做

- 区域光（球形光、管状光、Specular D Modification），见 5.2。
- 材质分层（Material Layering）。
- 局部反射探针混合（原文 split sum 的动机之一）。

### 6.4 一致性自检（IblBaker `--self-test`）

在常量环境 \(L_i = (0.25, 1, 4)\) 下检查以下几项，任何一项失败时程序返回非 0：

| 检查项 | 依据 | 容差 |
| --- | --- | --- |
| `PrefilterEnvMap` 在每个 mip 上都输出该常量 | 式 7 对常量 \(L_i\) 精确成立 | 相对误差 1e-4 |
| `SpecularIbl`（3.1 节参考积分）与 `Prefiltered * (F0 * A + B)` 一致，Roughness 和 NoV 各取 {0.05, 0.25, 0.5, 0.75, 1}，F0 取 {0.04, 1} | 式 7 + 3.5 节 | 相对误差 1e-3 |
| LUT 所有 texel 满足 `A + B ≤ 1 + ε`（`A + B` 是 F=1 时的方向反照率） | 式 8 | ε = 1e-3 |
| LUT 在最小 Roughness、最大 NoV 处趋近 `(1, 0)` | 镜面正视 | 1e-2 |
| 补充项：irradiance 输出该常量 | 6.2 节漫反射 IBL | 相对误差 1e-4 |
