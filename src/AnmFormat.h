#pragma once

#include<cstdint>

namespace Anmfmt
{
	constexpr char MAGIC[4] = { 'A','N','I','M' };
	constexpr uint32_t VERSION = 2u;

	struct Vec3 { float x, y, z; };
	struct Vec4 { float x, y, z, w; };

#pragma pack(push, 1)

	// ファイルヘッダー
	struct AnmHeader
	{
		char     magic[4];
		uint32_t version;
		uint32_t animationCount;
	};
	static_assert(sizeof(AnmHeader) == 12, "AnmHeader size mismatch");

	// アニメーションエントリー
	struct AnimEntry
	{
		char     name[64];
		float    duration;           // アニメーション総時間 (秒)
		uint32_t channelCount;
		uint32_t isBaked;            // 0 = スパースキー, 1 = ベイク済み均等フレーム
		float    bakeFrameRate;      // isBaked==1 時のFPS (例: 30.0, 60.0)
		// isBaked==0 時は 0.0
	};
	static_assert(sizeof(AnimEntry) == 80, "AnimEntry size mismatch");

	// スパースキー方式：isBaked == 0
	struct ChannelEntry
	{
		char     boneName[64];
		uint32_t posKeyCount;
		uint32_t rotKeyCount;
		uint32_t scaleKeyCount;
		// 直後に PosKey[], RotKey[], ScaleKey[]
	};
	static_assert(sizeof(ChannelEntry) == 76, "ChannelEntry size mismatch");

	struct PosKey { float time; Vec3 value; };   // 16 bytes
	struct RotKey { float time; Vec4 value; };   // 20 bytes
	struct ScaleKey { float time; Vec3 value; };   // 16 bytes
	static_assert(sizeof(PosKey) == 16, "PosKey size mismatch");
	static_assert(sizeof(RotKey) == 20, "RotKey size mismatch");
	static_assert(sizeof(ScaleKey) == 16, "ScaleKey size mismatch");

	// ベイク済み均等フレーム形式
	struct BakedChannelEntry
	{
		char     boneName[64];
		uint32_t frameCount;         // フレーム数 = ceil(duration * bakeFrameRate) + 1
		// 直後に BakedFrame[frameCount]
	};
	static_assert(sizeof(BakedChannelEntry) == 68, "BakedChannelEntry size mismatch");

	// TRS を1フレームにまとめた構造体
	// エンジン側では XMMatrixAffineTransformation() で行列に変換して使う
	struct BakedFrame
	{
		Vec3 translation;            // 12 bytes
		Vec4 rotation;               // 16 bytes  クォータニオン (x,y,z,w)
		Vec3 scale;                  // 12 bytes
		// 合計 40 bytes (パディングなし)
	};
	static_assert(sizeof(BakedFrame) == 40, "BakedFrame size mismatch");

#pragma pack(pop)

}

