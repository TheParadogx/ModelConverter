#include"Converter.h"

#include<assimp/Importer.hpp>
#include<assimp/postprocess.h>
#include<assimp/quaternion.h>
#include<assimp/matrix4x4.h>
#include<assimp/material.h>

#include<fstream>
#include<iostream>
#include<algorithm>
#include<cassert>
#include<cmath>
#include<cstring>


namespace
{

    // ストリーム書き込み
    template<typename T>
    void Write(std::ofstream& fs, const T& v)
    {
        fs.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template<typename T>
    void WriteVec(std::ofstream& fs, const std::vector<T>& v)
    {
        if (!v.empty())
            fs.write(reinterpret_cast<const char*>(v.data()), sizeof(T) * v.size());
    }

    // 文字列コピー
    template<size_t N>
    void SafeCopy(char(&dst)[N], const char* src)
    {
        if (!src) { dst[0] = '\0'; return; }

        strncpy_s(dst, N, src, _TRUNCATE);
    }

    // マテリアル
    std::string GetTexPath(const aiMaterial* mat, aiTextureType type)
    {
        aiString p;
        return (mat->GetTexture(type, 0, &p) == AI_SUCCESS) ? p.C_Str() : "";
    }

    // float配列 -> Mat4x4
    Binfmt::Mat4x4 ToMat4x4(const float m[4][4])
    {
        Binfmt::Mat4x4 r{};
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[i][j] = m[i][j];
        return r;
    }

    // アニメーション補完ユーティリティ
    
    // 平行移動/スケールキー
    aiVector3D EvalVec3(const aiVectorKey* keys, unsigned count, double t)
    {
        if (count == 0) return {};
        if (count == 1) return keys[0].mValue;
        if (t <= keys[0].mTime) return keys[0].mValue;
        if (t >= keys[count - 1].mTime) return keys[count - 1].mValue;

        // 二分探索で前後キーを取得
        unsigned hi = 1;
        while (hi < count - 1 && keys[hi].mTime < t) ++hi;
        unsigned lo = hi - 1;

        double span = keys[hi].mTime - keys[lo].mTime;
        float  alpha = (span > 1e-10) ? (float)((t - keys[lo].mTime) / span) : 0.0f;

        const aiVector3D& a = keys[lo].mValue;
        const aiVector3D& b = keys[hi].mValue;
        return aiVector3D(
            a.x + (b.x - a.x) * alpha,
            a.y + (b.y - a.y) * alpha,
            a.z + (b.z - a.z) * alpha);
    }

    // 回転クオータニオンを時刻でSLERP保管
    aiQuaternion EvalQuat(const aiQuatKey* keys, unsigned count, double t)
    {
        if (count == 0) return aiQuaternion(1, 0, 0, 0);
        if (count == 1) return keys[0].mValue;
        if (t <= keys[0].mTime) return keys[0].mValue;
        if (t >= keys[count - 1].mTime) return keys[count - 1].mValue;

        unsigned hi = 1;
        while (hi < count - 1 && keys[hi].mTime < t) ++hi;
        unsigned lo = hi - 1;

        double span = keys[hi].mTime - keys[lo].mTime;
        float  alpha = (span > 1e-10) ? (float)((t - keys[lo].mTime) / span) : 0.0f;

        aiQuaternion result;
        aiQuaternion::Interpolate(result, keys[lo].mValue, keys[hi].mValue, alpha);
        return result.Normalize();
    }



}


namespace conv
{
    // コンバート
    conv::ConvertResult conv::ModelConverter::Convert(const std::filesystem::path& inputPath, const std::filesystem::path& outputDir, const ConverterOptions& options)
    {
        mOptions = options;
        conv::ConvertResult result;

        namespace fs = std::filesystem;
        fs::path outDir = outputDir.empty() ? inputPath.parent_path() : outputDir;
        if (!fs::exists(outDir))
        {
            std::error_code ec;
            fs::create_directories(outDir, ec);
            if (ec) { result.errorMessage = "directory error"; return result; }
        }

        const std::string stem = inputPath.stem().string();
        Assimp::Importer importer;
        importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, mOptions.scale);

        const ::aiScene* scene = importer.ReadFile(inputPath.string(), BuildPostProcessFlags());
        if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
        {
            result.errorMessage = "Assimp load failed";
            return result;
        }

        // ボーン収集
        CollectBones(scene);

        // bin書き出し
        fs::path binPath = outDir / (stem + ".bin");
        if (!WriteBin(scene, binPath))
        {
            result.errorMessage = "bin failed";
            return result;
        }

        // anm書き出し
        bool hasAnim = (scene->mNumAnimations > 0);
        fs::path anmPath;
        if (hasAnim)
        {
            anmPath = outDir / (stem + ".anm");
            if (!WriteAnm(scene, anmPath))
            {
                result.errorMessage = "anm failed";
                return result;
            }
        }

        result.success = true;
        result.hasMesh = scene->mNumMeshes > 0;
        result.hasAnimation = hasAnim;
        result.hasSkeleton = !mBones.empty();
        result.meshCount = (int)scene->mNumMeshes;
        result.materialCount = (int)scene->mNumMaterials;
        result.boneCount = (int)mBones.size();
        result.animationCount = (int)scene->mNumAnimations;
        result.binPath = binPath.string();
        result.anmPath = hasAnim ? anmPath.string() : "";
        return result;
    }

    unsigned conv::ModelConverter::BuildPostProcessFlags() const
    {
        unsigned flags = 0;
        if (mOptions.triangulate)           flags |= aiProcess_Triangulate;
        if (mOptions.joinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;
        if (mOptions.generateNormals)       flags |= aiProcess_GenSmoothNormals;
        if (mOptions.generateTangents)      flags |= aiProcess_CalcTangentSpace;
        if (mOptions.optimizeMeshes)        flags |= aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph;
        if (mOptions.flipUVs)               flags |= aiProcess_FlipUVs;
        if (mOptions.convertToLeftHanded)   flags |= aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder;
        if (mOptions.scale != 1.0f)         flags |= aiProcess_GlobalScale;

        flags |= aiProcess_LimitBoneWeights;
        flags |= aiProcess_ImproveCacheLocality;
        return flags;
    }

    void conv::ModelConverter::CollectBones(const aiScene* scene)
    {
        mBones.clear();
        mBoneIndexMap.clear();

        // メッシュボーンのoffset行列マップ構築
        std::unordered_map<std::string, ::aiMatrix4x4> offsetMap;
        for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
        {
            const ::aiMesh* mesh = scene->mMeshes[mi];
            for (unsigned bi = 0; bi < mesh->mNumBones; ++bi)
            {
                const ::aiBone* b = mesh->mBones[bi];
                offsetMap.emplace(b->mName.C_Str(), b->mOffsetMatrix);
            }
        }

        // アニメーション
        std::unordered_set<std::string> skeletonNames;
        for (auto& [name, _] : offsetMap) skeletonNames.insert(name);
        for (unsigned ai = 0; ai < scene->mNumAnimations; ++ai)
        {
            const ::aiAnimation* anim = scene->mAnimations[ai];
            for (unsigned ci = 0; ci < anim->mNumChannels; ++ci)
                skeletonNames.insert(anim->mChannels[ci]->mNodeName.C_Str());
        }
        if (skeletonNames.empty()) return;

        // ルートからDFSしてスケルトン関連サブツリーを全取得
        TraverseSkeletonNodes(scene->mRootNode, skeletonNames, offsetMap, -1);


    }

    bool ModelConverter::ContainsSkeletonNode(const ::aiNode* node, const std::unordered_set<std::string>& names)
    {
        if (names.count(node->mName.C_Str())) return true;
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            if (ContainsSkeletonNode(node->mChildren[i], names)) return true;
        return false;
    }

    void ModelConverter::TraverseSkeletonNodes(const ::aiNode* node, const std::unordered_set<std::string>& skeletonNames, const std::unordered_map<std::string, ::aiMatrix4x4>& offsetMap, int parentIndex)
    {
        // このサブツリーにスケルトン関連ノードがなければ丸ごとスキップ
        if (!ContainsSkeletonNode(node, skeletonNames)) return;

        std::string name = node->mName.C_Str();
        int myIndex = (int)mBones.size();
        mBoneIndexMap[name] = myIndex;

        BoneInfo info;
        info.name = name;
        info.parentIndex = parentIndex;

        // offset 行列: ウェイトあり → aiMesh から, なし → 単位行列
        auto it = offsetMap.find(name);
        if (it != offsetMap.end())
        {
            AiMatToRowMajor(info.offsetMatrix, it->second);
        }
        else
        {
            // 単位行列 (Identity)
            aiMatrix4x4 identity;
            AiMatToRowMajor(info.offsetMatrix, identity);
            Log("    [中間ノード] " + name + " (offset = Identity)");
        }
        AiMatToRowMajor(info.localTransform, node->mTransformation);
        mBones.push_back(info);

        for (unsigned i = 0; i < node->mNumChildren; ++i)
            TraverseSkeletonNodes(node->mChildren[i], skeletonNames, offsetMap, myIndex);

    }

    bool ModelConverter::WriteBin(const ::aiScene* scene, const std::filesystem::path& outputPath)
    {
        std::ofstream fs(outputPath, std::ios::binary | std::ios::trunc);
        if (!fs.is_open()) return false;

        const bool hasSkeleton = !mBones.empty();

        // BinHeader
        Binfmt::BinHeader header{};
        std::memcpy(header.magic, Binfmt::MAGIC, 4);
        header.version = Binfmt::VERSION;
        header.meshCount = scene->mNumMeshes;
        header.materialCount = scene->mNumMaterials;
        header.boneCount = (uint32_t)mBones.size();
        header.flags = hasSkeleton ? Binfmt::Flags::HasSkeleton : 0u;
        Write(fs, header);

        // マテリアル
        for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
        {
            const aiMaterial* mat = scene->mMaterials[mi];
            Binfmt::MaterialEntry e{};

            aiString name;
            if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) SafeCopy(e.name, name.C_Str());

            aiColor4D col;
            e.baseColor = (mat->Get(AI_MATKEY_COLOR_DIFFUSE, col) == AI_SUCCESS)
                ? Binfmt::Vec4{ col.r,col.g,col.b,col.a } : Binfmt::Vec4{ 1,1,1,1 };

            if (mat->Get(AI_MATKEY_COLOR_SPECULAR, col) == AI_SUCCESS)
            {
                float sh = 32.0f; mat->Get(AI_MATKEY_SHININESS, sh);
                e.specular = { col.r, col.g, col.b, sh };
            }
            aiColor3D em;
            if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, em) == AI_SUCCESS)
                e.emissive = { em.r, em.g, em.b, 1.0f };


            /*
            * マクロが正常に動作しないので直接取得します。
            */
            // 粗さの取得
            if (mat->Get("$mat.roughnessFactor", 0, 0, e.roughness) != AI_SUCCESS)
            {
                e.roughness = 1.0f; // 取れなかった場合のデフォルト値
            }
            // 金属度の取得
            if (mat->Get("$mat.metallicFactor", 0, 0, e.metallic) != AI_SUCCESS)
            {
                e.metallic = 0.0f; // 取れなかった場合のデフォルト値
            }

            mat->Get(AI_MATKEY_OPACITY, e.opacity);
            if (e.opacity == 0.0f) e.opacity = 1.0f;

            int twoSided = 0;
            mat->Get(AI_MATKEY_TWOSIDED, twoSided);
            if (twoSided) e.flags |= 0x2u;
            auto d = GetTexPath(mat, aiTextureType_DIFFUSE);
            auto n = GetTexPath(mat, aiTextureType_NORMALS);
            auto s = GetTexPath(mat, aiTextureType_SPECULAR);
            auto em2 = GetTexPath(mat, aiTextureType_EMISSIVE);
            SafeCopy(e.diffuseTex, d.c_str());
            SafeCopy(e.normalTex, n.c_str());
            SafeCopy(e.specularTex, s.c_str());
            SafeCopy(e.emissiveTex, em2.c_str());

            Write(fs, e);
            Log("  [Mat] " + std::string(e.name));
        }

        // BoneEntry
        for (const auto& b : mBones)
        {
            Binfmt::BoneEntry e{};
            SafeCopy(e.name, b.name.c_str());
            e.parentIndex = b.parentIndex;
            e.offsetMatrix = ToMat4x4(b.offsetMatrix);
            e.localTransform = ToMat4x4(b.localTransform);
            Write(fs, e);
        }

        // MeshChunk
        for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi)
        {
            const ::aiMesh* mesh = scene->mMeshes[mi];
            const uint32_t vCount = mesh->mNumVertices;
            const bool use32 = (vCount > 65535u);

            // 頂点構築
            std::vector<Binfmt::Vertex> verts(vCount);
            for (uint32_t vi = 0; vi < vCount; ++vi)
            {
                auto& v = verts[vi];
                auto& p = mesh->mVertices[vi];
                v.position = { p.x, p.y, p.z };
                if (mesh->HasNormals())
                {
                    auto& n = mesh->mNormals[vi]; v.normal = { n.x,n.y,n.z };
                }
                if (mesh->HasTextureCoords(0))
                {
                    auto& u = mesh->mTextureCoords[0][vi]; v.texcoord0 = { u.x,u.y };
                }
                if (mesh->HasTextureCoords(1))
                {
                    auto& u = mesh->mTextureCoords[1][vi]; v.texcoord1 = { u.x,u.y };
                }
                if (mesh->HasTangentsAndBitangents())
                {
                    auto& t = mesh->mTangents[vi];   v.tangent = { t.x,t.y,t.z };
                    auto& b = mesh->mBitangents[vi]; v.bitangent = { b.x,b.y,b.z };
                }
                // boneIndices / boneWeights は後で設定
            }

            // スキニング：各頂点を収集（index,weight）
            using WList = std::vector<std::pair<uint32_t, float>>;
            std::vector<WList> skin(vCount);
            for (unsigned bi = 0; bi < mesh->mNumBones; ++bi)
            {
                const ::aiBone* bone = mesh->mBones[bi];
                auto it = mBoneIndexMap.find(bone->mName.C_Str());
                if (it == mBoneIndexMap.end()) continue;
                uint32_t gIdx = (uint32_t)it->second;
                for (unsigned wi = 0; wi < bone->mNumWeights; ++wi)
                {
                    uint32_t vid = bone->mWeights[wi].mVertexId;
                    float    w = bone->mWeights[wi].mWeight;
                    if (vid < vCount) skin[vid].emplace_back(gIdx, w);
                }
            }
            for (uint32_t vi = 0; vi < vCount; ++vi) 
            {
                auto& list = skin[vi];
                std::sort(list.begin(), list.end(),
                    [](const auto& a, const auto& b) { return a.second > b.second; });
                float tw = 0.0f;
                int cnt = std::min((int)list.size(), 4);
                for (int b = 0; b < cnt; ++b)
                {
                    // Fix3: uint8_t に変換 (0〜254, 255 は無効インデックス用に予約可)
                    verts[vi].boneIndices[b] = (uint8_t)std::min(list[b].first, 254u);
                    verts[vi].boneWeights[b] = list[b].second;
                    tw += list[b].second;
                }
                if (tw > 1e-6f)
                {
                    for (int b = 0; b < cnt; ++b) verts[vi].boneWeights[b] /= tw;
                }
            }

            // インデックス
            const uint32_t iCount = mesh->mNumFaces * 3;
            std::vector<uint32_t> idx32;
            std::vector<uint16_t> idx16;
            if (use32)
            {
                idx32.reserve(iCount); 
            }
            else
            {
                idx16.reserve(iCount);
            }
            for (unsigned fi = 0; fi < mesh->mNumFaces; ++fi) 
            {
                assert(mesh->mFaces[fi].mNumIndices == 3);
                for (unsigned ii = 0; ii < 3; ++ii)
                {
                    uint32_t idx = mesh->mFaces[fi].mIndices[ii];
                    if (use32) idx32.push_back(idx);
                    else       idx16.push_back((uint16_t)idx);
                }
            }

            // 書き出し
            Binfmt::MeshEntry me = {};
            SafeCopy(me.name, mesh->mName.C_Str());
            me.vertexCount = vCount;
            me.indexCount = iCount;
            me.materialIndex = mesh->mMaterialIndex;
            me.use32BitIndex = use32 ? 1u : 0u;
            Write(fs, me);
            WriteVec(fs, verts);
            if (use32) WriteVec(fs, idx32); else WriteVec(fs, idx16);

            Log("  [Mesh] " + std::string(mesh->mName.C_Str()) +
                " vtx=" + std::to_string(vCount) +
                " idx=" + std::to_string(iCount));

        }

        Log("[BIN] 書き出し完了: " + outputPath.string());
        return true;
    }

    // .anmの出力
    bool ModelConverter::WriteAnm(const ::aiScene* scene, const std::filesystem::path& outputPath)
    {
        if (scene->mNumAnimations == 0) return true;

        std::ofstream fs(outputPath, std::ios::binary | std::ios::trunc);
        if (!fs.is_open()) return false;

        const bool doBake = (mOptions.bakeFPS > 0.0f);

        Anmfmt::AnmHeader header = {};
        std::memcpy(header.magic, Anmfmt::MAGIC, 4);
        header.version = Anmfmt::VERSION;
        header.animationCount = scene->mNumAnimations;
        Write(fs, header);

        for (unsigned ai = 0; ai < scene->mNumAnimations; ++ai)
        {
            const aiAnimation* anim = scene->mAnimations[ai];
            double tps = (anim->mTicksPerSecond > 0.0) ? anim->mTicksPerSecond : 25.0;
            double durationTicks = anim->mDuration;
            float  durationSec = (float)(durationTicks / tps);

            Anmfmt::AnimEntry entry{};
            SafeCopy(entry.name, anim->mName.C_Str());
            entry.duration = durationSec;
            entry.channelCount = anim->mNumChannels;
            entry.isBaked = doBake ? 1u : 0u;
            entry.bakeFrameRate = doBake ? mOptions.bakeFPS : 0.0f;
            Write(fs, entry);
        }

        return true;
    }

    void conv::ModelConverter::Log(const std::string& msg) const
    {
    }

    void conv::ModelConverter::AiMatToRowMajor(float dst[4][4], const ::aiMatrix4x4& src)
    {
        // 1行目 (a1, a2, a3, a4)
        dst[0][0] = src.a1; dst[0][1] = src.a2; dst[0][2] = src.a3; dst[0][3] = src.a4;
        // 2行目 (b1, b2, b3, b4)
        dst[1][0] = src.b1; dst[1][1] = src.b2; dst[1][2] = src.b3; dst[1][3] = src.b4;
        // 3行目 (c1, c2, c3, c4)
        dst[2][0] = src.c1; dst[2][1] = src.c2; dst[2][2] = src.c3; dst[2][3] = src.c4;
        // 4行目 (d1, d2, d3, d4)
        dst[3][0] = src.d1; dst[3][1] = src.d2; dst[3][2] = src.d3; dst[3][3] = src.d4;
    }
}

