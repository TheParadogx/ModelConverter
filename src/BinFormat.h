#pragma once

#include<cstdint>

namespace Binfmt
{
	// マジック、バージョン
	constexpr char MAGIC[4] = { 'M','D','L','B' };
	constexpr uint32_t VERSION = 2u;

	// プラットフォーム非依存の数学系（bit配置が一致してたらなんでもいい）
	struct Vec2 { float x, y; };
	struct Vec3 { float x, y, z; };
	struct Vec4 { float x, y, z, w; };
	struct Mat4x4 { float m[4][4]; };

#pragma pack(push, 1)

	// 頂点
	struct Vertex
	{
		Vec3    position;           // 12
		Vec3    normal;             // 12
		Vec2    texcoord0;          //  8
		Vec2    texcoord1;          //  8
		Vec3    tangent;            // 12
		Vec3    bitangent;          // 12
		uint8_t boneIndices[4];     //  4
		float   boneWeights[4];     // 16
		// 合計 84 bytes
	};
	static_assert(sizeof(Vertex) == 84, "Vertex size mismatch");

	// ファイルヘッダー
	struct BinHeader
	{
		char     magic[4];
		uint32_t version;
		uint32_t meshCount;
		uint32_t materialCount;
		uint32_t boneCount;
		uint32_t flags;
	};
	static_assert(sizeof(BinHeader) == 24, "BinHeader size mismatch");

	// マテリアル
	struct MaterialEntry
	{
		char     name[64];
		Vec4     baseColor;
		Vec4     specular;
		Vec4     emissive;
		float    roughness;
		float    metallic;
		float    opacity;
		uint32_t flags;
		char     diffuseTex[256];
		char     normalTex[256];
		char     specularTex[256];
		char     emissiveTex[256];
	};
	static_assert(sizeof(MaterialEntry) == 1152, "MaterialEntry size mismatch");

	// ボーン
	// ウェイドを持たない中間・ダミーボーンも登録する。
	// その場合は単位行列として扱う。
	struct BoneEntry
	{
		char    name[64];
		int32_t parentIndex;        // -1 = ルートボーン
		Mat4x4  offsetMatrix;       // Inverse Bind Pose (モデル空間 → ボーン空間)
		// ウェイトなし中間ノードは Identity
		Mat4x4  localTransform;     // レスト姿勢でのローカル変換
	};
	static_assert(sizeof(BoneEntry) == 196, "BoneEntry size mismatch");

	// メッシュチャンク
	// Dx12側のインデックスバッファの設定について
	// use32BitIndex==0 → DXGI_FORMAT_R16_UINT
	// use32BitIndex==1 → DXGI_FORMAT_R32_UINT
	struct MeshEntry
	{
		char     name[64];
		uint32_t vertexCount;
		uint32_t indexCount;
		uint32_t materialIndex;     // 0xFFFFFFFF = マテリアルなし
		uint32_t use32BitIndex;     // 0 = uint16_t, 1 = uint32_t
		uint32_t reserved[4];
	};
	static_assert(sizeof(MeshEntry) == 96, "MeshEntry size mismatch");

	// スケルトンのフラグ
	namespace Flags { constexpr uint32_t HasSkeleton = 1u << 0; };

#pragma pack(pop)
}
