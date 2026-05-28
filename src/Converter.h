#pragma once

#include<string>
#include<vector>
#include<filesystem>
#include<unordered_map>
#include<unordered_set>
#include<functional>

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
		using LogCallback = std::function<void(const std::string&)>;
		void SetLogCallback(LogCallback cb);

		ConvertResult Convert(
			const std::filesystem::path& inputPath,
			const std::filesystem::path& outputDir = {},
			const ConverterOptions& options = {});
	private:
		// ボーンの内部処理用
		struct BoneInfo 
		{
			std::string name;
			int parentIndex = -1;
			float offsetMatrix[4][4];
			float localTransform[4][4];
		};

		// ステート
		std::vector<BoneInfo> mBones;
		std::unordered_map<std::string, int> mBoneIndexMap;
		ConverterOptions mOptions;
		LogCallback mLogCb;

		// ヘルパー
		unsigned BuildPostProcessFlags() const;

		// ノードベースのボーン収集
		void CollectBones(const ::aiScene* scene);

		// node 以下にスケルトン関連ノードが含まれるか再帰チェック
		static bool ContainsSkeletonNode(const ::aiNode* node, const std::unordered_set<std::string>& names);

		// スケルトンノードを DFS で全収集 (中間ノード・ダミーノードも含む)
		void TraverseSkeletonNodes(
			const ::aiNode* node,
			const std::unordered_set<std::string>& skeletonNames, 
			const std::unordered_map<std::string, ::aiMatrix4x4>& offsetMap,
			int parentIndex);

		// bin書き出し
		bool WriteBin(
			const ::aiScene* scene,
			const std::filesystem::path& outputPath);

		// Anm書き出し
		bool WriteAnm(
			const ::aiScene* scene,
			const std::filesystem::path& outputPath
		);

		// ログ
		void Log(const std::string& msg) const;

		static void AiMatToRowMajor(float dst[4][4], const ::aiMatrix4x4& src);

	};
}