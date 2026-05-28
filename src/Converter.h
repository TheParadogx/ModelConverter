#pragma once

#include<string>
#include<vector>
#include<filesystem>
#include<unordered_map>

#include<assimp/matrix4x4.h>
#include<assimp/scene.h>

#include"AnmFormat.h"
#include"BinFormat.h"

namespace conv
{
	// 変換オプション
	struct ConverterOptions
	{
		// 座標系
		bool convertToLeftHanded = true;

		// UV
		bool flipUVs = true;

		// 法線、接線
		bool generateNormals = true;
		bool generateTangents = true;

		// メッシュ最適化
		bool joinIdenticalVertices = true;
		bool optimizeMeshes = true;
		bool triangulate = true;

		// スケール
		float scale = 1.0f;

		// アニメーションベイク
		// 0.0f = スペースキーをそのまま保存
		//  > 0.0f = 指定FPSで均等サンプリング
		float bakeFPS = 30.0f;

		// ログ
		bool verbose = false;
	};

	// 変換結果
	struct ConvertResult
	{
		bool        success = false;
		bool        hasMesh = false;
		bool        hasAnimation = false;
		bool        hasSkeleton = false;
		int         meshCount = 0;
		int         materialCount = 0;
		int         boneCount = 0;
		int         animationCount = 0;
		std::string binPath;
		std::string anmPath;
		std::string errorMessage;
	};

	// コンバーター
	class ModelConverter
	{
	public:

	private:

	};
}