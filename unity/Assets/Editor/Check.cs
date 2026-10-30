using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

public class Check
{
    public static void Material()
    {
        var paint = new Material(Shader.Find("Universal Render Pipeline/Lit"));
        AssetDatabase.CreateAsset(paint, "Assets/Resources/base.mat");
        AssetDatabase.SaveAssets();
    }

    public static void Pipeline()
    {
        var renderer = ScriptableObject.CreateInstance<UniversalRendererData>();
        AssetDatabase.CreateAsset(renderer, "Assets/renderer.asset");
        var pipeline = UniversalRenderPipelineAsset.Create(renderer);
        AssetDatabase.CreateAsset(pipeline, "Assets/pipeline.asset");
        GraphicsSettings.defaultRenderPipeline = pipeline;
        QualitySettings.renderPipeline = pipeline;
        AssetDatabase.SaveAssets();
    }

    public static void Save()
    {
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        new GameObject("rougarou").AddComponent<Main>();
        EditorSceneManager.SaveScene(scene, "Assets/main.unity");
        EditorBuildSettings.scenes = new[] { new EditorBuildSettingsScene("Assets/main.unity", true) };
    }

    public static void Run()
    {
        Main.Probe();
    }

    public static void Rig()
    {
        var resource = Resources.Load<GameObject>("blaize");
        Debug.Log(resource == null ? "Resources.Load<blaize> NULL" : $"Resources.Load<blaize> ok: {resource.name}");
        var all = Resources.LoadAll<GameObject>("");
        Debug.Log($"resources gameobjects: {all.Length} :: {string.Join(", ", System.Array.ConvertAll(all, x => x.name))}");
        var model = AssetDatabase.LoadAssetAtPath<GameObject>("Assets/Resources/blaize.fbx");
        Debug.Log(model == null ? "AssetDatabase blaize NULL" : $"AssetDatabase blaize ok: {model.name}");
        var model2 = AssetDatabase.LoadAssetAtPath<GameObject>("Assets/characters/blaize.fbx");
        if (model == null) throw new System.Exception("blaize.fbx failed to import");
        var bones = model.GetComponentsInChildren<Transform>(true);
        var names = new System.Collections.Generic.List<string>();
        foreach (var bone in bones) names.Add(bone.name);
        Debug.Log($"blaize rig nodes: {bones.Length} :: {string.Join(", ", names.GetRange(0, System.Math.Min(30, names.Count)))}");
        var skin = model.GetComponentInChildren<SkinnedMeshRenderer>(true);
        Debug.Log(skin == null ? "no skinned mesh" : $"skinned mesh: {skin.sharedMesh.vertexCount} verts {skin.bones.Length} bones bounds={skin.bounds.size} local={skin.sharedMesh.bounds.size}");
        var hips = model.transform.Find("mixamorig:Hips");
        if (hips != null) Debug.Log($"hips local pos: {hips.localPosition} scale: {model.transform.localScale}");
    }

    public static void Build()
    {
        var output = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "builds", "rougarou-unity.app"));
        var options = new BuildPlayerOptions
        {
            scenes = new[] { "Assets/main.unity" },
            locationPathName = output,
            target = BuildTarget.StandaloneOSX,
            options = BuildOptions.None
        };
        var result = BuildPipeline.BuildPlayer(options);
        if (result.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded)
            throw new System.Exception(result.summary.result.ToString());
    }
}
