using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;

[Serializable]
public class Hold
{
    public float x;
    public float y;
    public string kind;
    public float dx;
    public float dy;
}

[Serializable]
public class Route
{
    public string name;
    public Hold[] holds;
}

[Serializable]
public class Spec
{
    public float gravity;
    public float mass;
    public float stiffness;
    public float damping;
    public float shift;
    public float reach;
    public Route[] routes;
}

public class Main : MonoBehaviour
{
    readonly List<GameObject> objects = new();
    readonly int[] grips = { 0, 1, 2, 3 };
    readonly float[] slips = new float[4];
    readonly Vector3[] points = new Vector3[16];
    readonly Vector3[] shown = new Vector3[4];
    readonly Renderer[] marks = new Renderer[16];
    readonly Transform[,] links = new Transform[4, 2];
    readonly Transform[] bends = new Transform[4];
    readonly Transform[] tips = new Transform[4];
    readonly Renderer[] ends = new Renderer[4];
    readonly Vector3[] posts = new Vector3[4];
    readonly bool[] smeared = new bool[4];
    static readonly string[,] chains =
    {
        { "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot" },
        { "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot" },
        { "mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand" },
        { "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand" }
    };
    readonly Transform[,] bones = new Transform[4, 3];
    readonly Transform[] clavs = new Transform[2];
    readonly Transform[] toes = new Transform[2];
    readonly Quaternion[] tiprel = new Quaternion[4];
    readonly float[] reallen = new float[4];
    static readonly string[] names = { "right foot", "left foot", "right hand", "left hand" };
    static readonly string[] labels = { "rf", "lf", "rh", "lh" };
    readonly Vector3[] seats = new Vector3[4];
    Transform[] joints;
    Quaternion[] binds;
    Vector3 rigoffset;
    Quaternion facing;

    Spec spec;
    Rigidbody body;
    Camera view;
    Renderer avatar;
    Material paint;
    MaterialPropertyBlock block;
    Vector3 aim;
    Vector2 shift;
    Vector2 cursor;
    float slope;
    float until;
    float minx;
    float maxx;
    float top;
    int route;
    int limb = 2;
    bool close;
    bool tracked;
    bool pose;
    bool won;
    string hint;
    Transform pointer;
    Transform rig;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Launch()
    {
        if (FindFirstObjectByType<Main>() == null)
            new GameObject("rougarou").AddComponent<Main>();
    }

    void Clear(UnityEngine.Object item)
    {
        if (Application.isPlaying) Destroy(item);
        else DestroyImmediate(item);
    }

    void Start()
    {
        spec = JsonUtility.FromJson<Spec>(Resources.Load<TextAsset>("routes").text);
        paint = Resources.Load<Material>("base");
        block = new MaterialPropertyBlock();
        Physics.gravity = Vector3.down * spec.gravity;
        Time.fixedDeltaTime = 1f / 60f;
        view = Camera.main;
        if (view == null)
        {
            view = new GameObject("camera").AddComponent<Camera>();
            view.tag = "MainCamera";
        }
        view.fieldOfView = 56;
        view.backgroundColor = new Color(0.065f, 0.08f, 0.11f);
        view.clearFlags = CameraClearFlags.SolidColor;
        RenderSettings.ambientLight = new Color(0.65f, 0.7f, 0.8f);
        var light = new GameObject("light").AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.6f;
        light.transform.rotation = Quaternion.Euler(35, -25, 0);
        objects.Add(light.gameObject);
        Cube(new Vector3(0, -0.18f, 1.5f), new Vector3(12, 0.25f, 10), new Color(0.15f, 0.19f, 0.22f));
        Build();
    }

    void Tint(Renderer renderer, Color color)
    {
        if (renderer.sharedMaterial != paint) renderer.sharedMaterial = paint;
        block.Clear();
        block.SetColor("_BaseColor", color);
        renderer.SetPropertyBlock(block);
    }

    GameObject Cube(Vector3 point, Vector3 size, Color color)
    {
        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        cube.transform.position = point;
        cube.transform.localScale = size;
        Tint(cube.GetComponent<Renderer>(), color);
        objects.Add(cube);
        return cube;
    }

    float Surface(float y) => 0.15f + (y - 1.45f) * slope;

    const float gap = 0.3f;

    void Build()
    {
        if (body != null)
        {
            for (var i = 2; i < objects.Count; i++)
                Clear(objects[i]);
            objects.RemoveRange(2, objects.Count - 2);
        }
        slope = route == 1 ? 0.25f : 0;
        var cx = route == 2 ? 0.7f : 0;
        minx = cx - 2.7f;
        maxx = cx + 2.7f;
        top = 4.4f;
        var wall = Cube(new Vector3(cx, 2.15f, Surface(2.15f) - 0.14f), new Vector3(5.4f, 4.7f, 0.3f), new Color(0.28f, 0.31f, 0.34f));
        wall.transform.rotation = Quaternion.Euler(Mathf.Atan(slope) * Mathf.Rad2Deg, 0, 0);
        var holds = spec.routes[route].holds;
        for (var i = 0; i < holds.Length; i++)
        {
            var hold = holds[i];
            points[i] = new Vector3(hold.x, hold.y, Surface(hold.y) + 0.05f);
            var grip = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            Clear(grip.GetComponent<Collider>());
            var radius = hold.kind == "sloper" ? 0.18f : 0.135f;
            grip.transform.position = points[i];
            grip.transform.localScale = Vector3.one * radius * 2;
            Tint(grip.GetComponent<Renderer>(), hold.kind == "jug" ? new Color(0.98f, 0.56f, 0.17f) : hold.kind == "crimp" ? new Color(0.9f, 0.3f, 0.45f) : new Color(0.6f, 0.45f, 0.9f));
            marks[i] = grip.GetComponent<Renderer>();
            objects.Add(grip);
        }
        var torso = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        torso.transform.position = new Vector3(0, 1.25f, Surface(1.25f) + gap);
        torso.transform.localScale = new Vector3(0.44f, 0.45f, 0.44f);
        Tint(torso.GetComponent<Renderer>(), new Color(0.09f, 0.8f, 0.7f));
        avatar = torso.GetComponent<Renderer>();
        // hips ride close to the wall, so the capsule must not wedge into overhangs
        Physics.IgnoreCollision(torso.GetComponent<Collider>(), wall.GetComponent<Collider>());
        body = torso.AddComponent<Rigidbody>();
        body.mass = spec.mass;
        body.linearDamping = 0.55f;
        body.angularDamping = 6;
        body.constraints = RigidbodyConstraints.FreezePositionZ | RigidbodyConstraints.FreezeRotationX | RigidbodyConstraints.FreezeRotationY;
        body.interpolation = RigidbodyInterpolation.Interpolate;
        objects.Add(torso);
        for (var i = 0; i < 4; i++)
        {
            for (var part = 0; part < 2; part++)
            {
                var link = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
                Clear(link.GetComponent<Collider>());
                Tint(link.GetComponent<Renderer>(), i >= 2 ? new Color(0.95f, 0.83f, 0.66f) : new Color(0.16f, 0.57f, 0.7f));
                links[i, part] = link.transform;
                objects.Add(link);
            }
            var bend = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            Clear(bend.GetComponent<Collider>());
            bend.transform.localScale = Vector3.one * 0.15f;
            Tint(bend.GetComponent<Renderer>(), i >= 2 ? new Color(0.95f, 0.83f, 0.66f) : new Color(0.16f, 0.57f, 0.7f));
            bends[i] = bend.transform;
            objects.Add(bend);
            var tip = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            Clear(tip.GetComponent<Collider>());
            tip.transform.localScale = Vector3.one * 0.21f;
            tips[i] = tip.transform;
            ends[i] = tip.GetComponent<Renderer>();
            objects.Add(tip);
        }
        Spawn();
        var marker = GameObject.CreatePrimitive(PrimitiveType.Sphere);
        Clear(marker.GetComponent<Collider>());
        marker.transform.localScale = Vector3.one * 0.16f;
        Tint(marker.GetComponent<Renderer>(), new Color(1, 1, 0.65f));
        pointer = marker.transform;
        objects.Add(marker);
        Restart();
        view.transform.position = body.position + (close ? new Vector3(0, 0.28f, 0.35f) : new Vector3(0, 0.22f, 5.8f));
    }

    void Spawn()
    {
        var prefab = Resources.Load<GameObject>("blaize");
        if (prefab == null) return;
        rig = Instantiate(prefab).transform;
        rig.name = "climber";
        var animator = rig.GetComponent<Animator>();
        if (animator != null) animator.enabled = false;
        rig.localScale = Vector3.one;
        rig.rotation = Quaternion.Euler(0, 180, 0);
        rig.position = Vector3.zero;
        foreach (var skin in rig.GetComponentsInChildren<SkinnedMeshRenderer>(true))
        {
            skin.localBounds = new Bounds(Vector3.zero, Vector3.one * 4);
            skin.updateWhenOffscreen = true;
            Tint(skin, new Color(0.82f, 0.72f, 0.6f));
        }
        // the fbx imports frozen mid-animation (hunched, yawed, legs crossed); put every bone back on its skin bind pose
        var bound = rig.GetComponentInChildren<SkinnedMeshRenderer>(true);
        if (bound != null)
        {
            var order = new System.Collections.Generic.List<int>();
            for (var i = 0; i < bound.bones.Length; i++) if (bound.bones[i] != null) order.Add(i);
            int Depth(Transform node) { var depth = 0; for (; node != null; node = node.parent) depth++; return depth; }
            order.Sort((x, y) => Depth(bound.bones[x]).CompareTo(Depth(bound.bones[y])));
            foreach (var i in order)
            {
                var pose = bound.transform.localToWorldMatrix * bound.sharedMesh.bindposes[i].inverse;
                bound.bones[i].SetPositionAndRotation(pose.GetColumn(3), pose.rotation);
            }
        }
        var map = new System.Collections.Generic.Dictionary<string, Transform>();
        foreach (var node in rig.GetComponentsInChildren<Transform>(true)) map[node.name] = node;
        for (var i = 0; i < 4; i++)
            for (var part = 0; part < 3; part++)
                map.TryGetValue(chains[i, part], out bones[i, part]);
        // mesh bounds import as zero, so measure height from the skeleton
        var floor = bones[0, 2] != null ? Mathf.Min(bones[0, 2].position.y, bones[1, 2].position.y) : 0;
        var height = map.TryGetValue("mixamorig:HeadTop_End", out var crown) ? crown.position.y - floor : 1;
        rig.localScale = Vector3.one * (1.85f / Mathf.Max(height, 0.0001f));
        if (map.TryGetValue("mixamorig:RightShoulder", out var rsh)) clavs[0] = rsh;
        if (map.TryGetValue("mixamorig:LeftShoulder", out var lsh)) clavs[1] = lsh;
        if (map.TryGetValue("mixamorig:RightToeBase", out var rtoe)) toes[0] = rtoe;
        if (map.TryGetValue("mixamorig:LeftToeBase", out var ltoe)) toes[1] = ltoe;
        if (bones[0, 0] == null || bones[1, 0] == null || bones[2, 0] == null || bones[3, 0] == null) { rig.gameObject.SetActive(false); rig = null; return; }
        // the fbx rest pose is yawed, so square the shoulder and hip lines to the wall
        var across = bones[1, 0].position + bones[3, 0].position - bones[0, 0].position - bones[2, 0].position;
        across.y = 0;
        facing = Quaternion.FromToRotation(across.normalized, Vector3.right) * rig.rotation;
        rig.rotation = facing;
        var center = Vector3.zero;
        for (var i = 0; i < 4; i++)
        {
            if (bones[i, 0] == null || bones[i, 1] == null || bones[i, 2] == null) { rig.gameObject.SetActive(false); rig = null; return; }
            center += bones[i, 0].position * 0.25f;
            reallen[i] = (Vector3.Distance(bones[i, 0].position, bones[i, 1].position) + Vector3.Distance(bones[i, 1].position, bones[i, 2].position)) * 0.5f;
            tiprel[i] = Quaternion.Inverse(bones[i, 1].rotation) * bones[i, 2].rotation;
        }
        // the game body is the rig's limb roots: joints and limb lengths come from the skeleton
        var lean = new Vector3(0, 0, -0.16f);
        rigoffset = rig.position - center + lean;
        for (var i = 0; i < 4; i++) seats[i] = bones[i, 0].position - center + lean;
        joints = rig.GetComponentsInChildren<Transform>(true);
        binds = new Quaternion[joints.Length];
        for (var i = 0; i < joints.Length; i++) binds[i] = joints[i].localRotation;
        objects.Add(rig.gameObject);
    }

    void Restart()
    {
        body.position = new Vector3(0, 1.25f, Surface(1.25f) + gap);
        body.rotation = Quaternion.identity;
        body.linearVelocity = Vector3.zero;
        body.angularVelocity = Vector3.zero;
        for (var i = 0; i < 4; i++)
        {
            grips[i] = i;
            slips[i] = 0;
            smeared[i] = false;
        }
        limb = 2;
        pose = false;
        won = false;
        aim = points[4];
        for (var i = 0; i < 4; i++) shown[i] = points[i];
        Notice("click a hand or foot, move the mouse, then click a hold");
    }

    Vector3 Joint(int index)
    {
        var side = index % 2 == 0 ? -1 : 1;
        var offset = rig != null ? seats[index] : new Vector3(side * (index >= 2 ? 0.26f : 0.17f), index >= 2 ? 0.25f : -0.24f, -0.16f);
        return body.position + body.rotation * offset;
    }

    Vector3 Rest(int index)
    {
        var side = index % 2 == 0 ? -1 : 1;
        // where a relaxed hold sits relative to the body: limbs most of the way extended, hands overhead, feet below
        if (rig != null) return seats[index] + new Vector3(side * 0.06f, index >= 2 ? Length(index) * 1.0f : -Length(index) * 2.0f, 0);
        return new Vector3(side * (index >= 2 ? 0.4f : 0.3f), index >= 2 ? 0.35f : -0.55f, -0.65f);
    }

    float Length(int index) => rig != null ? reallen[index] : index >= 2 ? 0.6f : 0.63f;

    float Range(int index) => Mathf.Min(spec.reach, Length(index) * 2 - 0.02f);

    void Drop(int index)
    {
        grips[index] = -1;
        smeared[index] = false;
    }

    void Notice(string message)
    {
        hint = message;
        until = Time.unscaledTime + 2.5f;
    }

    int Nearest(Vector3 target)
    {
        var candidate = -1;
        var distance = 0.35f;
        for (var i = 0; i < spec.routes[route].holds.Length; i++)
        {
            var delta = Vector2.Distance(new Vector2(points[i].x, points[i].y), new Vector2(target.x, target.y));
            if (delta < distance)
            {
                candidate = i;
                distance = delta;
            }
        }
        return candidate;
    }

    bool Grab(int index)
    {
        var candidate = Nearest(aim);
        if (candidate >= 0)
        {
            if (Vector3.Distance(Joint(index), points[candidate]) > Range(index))
            {
                Notice($"{names[index]} cannot reach: shift your hips or move a foot");
                return false;
            }
            grips[index] = candidate;
            smeared[index] = false;
            slips[index] = 0;
            Notice($"{names[index]} placed on {spec.routes[route].holds[candidate].kind}");
            return true;
        }
        var press = new Vector3(aim.x, aim.y, Surface(aim.y) + 0.05f);
        if (aim.x < minx || aim.x > maxx || aim.y < 0.05f || aim.y > top)
        {
            Notice("no hold under the tip");
            return false;
        }
        if (Vector3.Distance(Joint(index), press) > Range(index))
        {
            Notice($"{names[index]} cannot reach the wall");
            return false;
        }
        grips[index] = -1;
        smeared[index] = true;
        posts[index] = press;
        slips[index] = 0;
        Notice(index < 2 ? $"{names[index]} {(Mathf.Abs(aim.x - body.position.x) > 0.45f ? "flagged" : "smeared")} on the wall" : $"{names[index]} pressed on the wall");
        return true;
    }

    Vector3 Bend(Vector3 start, Vector3 end, int index) => Bend(start, end, index, Length(index));

    Vector3 Bend(Vector3 start, Vector3 end, int index, float length)
    {
        var delta = end - start;
        var distance = Mathf.Clamp(delta.magnitude, 0.01f, length * 2 - 0.001f);
        var direction = delta.normalized;
        var side = index % 2 == 0 ? -1 : 1;
        var bias = body.rotation * (index >= 2 ? new Vector3(side * 0.5f, -0.6f, 0.6f) : new Vector3(side, 0.1f, 0.25f));
        var normal = (bias - direction * Vector3.Dot(bias, direction)).normalized;
        var middle = distance * 0.5f;
        return start + direction * middle + normal * Mathf.Sqrt(Mathf.Max(0, length * length - middle * middle));
    }

    static float Distance(Vector2 point, Vector2 start, Vector2 end)
    {
        var direction = end - start;
        var length = direction.sqrMagnitude;
        var fraction = length > 0 ? Mathf.Clamp01(Vector2.Dot(point - start, direction) / length) : 0;
        return Vector2.Distance(point, start + direction * fraction);
    }

    int Pick(Vector2 position)
    {
        var candidate = -1;
        var closest = 42f;
        for (var i = 0; i < 4; i++)
        {
            var point = view.WorldToScreenPoint(shown[i]);
            if (point.z <= 0) continue;
            var distance = Vector2.Distance(position, new Vector2(point.x, point.y));
            if (distance >= closest) continue;
            closest = distance;
            candidate = i;
        }
        if (candidate >= 0) return candidate;
        closest = 22f;
        for (var i = 0; i < 4; i++)
        {
            var start = view.WorldToScreenPoint(Joint(i));
            var middle = view.WorldToScreenPoint(Bend(Joint(i), shown[i], i));
            var end = view.WorldToScreenPoint(shown[i]);
            if (start.z <= 0 || end.z <= 0) continue;
            var distance = Mathf.Min(Distance(position, start, middle), Distance(position, middle, end));
            if (distance >= closest) continue;
            closest = distance;
            candidate = i;
        }
        return candidate;
    }

    void Aim(Vector2 position)
    {
        var ray = view.ScreenPointToRay(position);
        var denominator = ray.direction.z - ray.direction.y * slope;
        if (Mathf.Abs(denominator) < 0.001f) return;
        var travel = (Surface(ray.origin.y) - ray.origin.z) / denominator;
        if (travel > 0) aim = ray.GetPoint(travel);
    }

    void Press(Vector2 position)
    {
        if (pose)
        {
            Aim(position);
            if (Nearest(aim) >= 0)
            {
                if (Grab(limb)) pose = false;
                return;
            }
        }
        var selected = Pick(position);
        if (selected >= 0)
        {
            limb = selected;
            pose = true;
            aim = shown[limb];
            cursor = position;
            tracked = true;
            Notice($"{names[limb]} selected: move the mouse to aim, click a hold or the wall");
            return;
        }
        if (!pose)
        {
            Notice("click a hand or foot to select it first");
            return;
        }
        Notice("no hold under the tip");
    }

    void Move(Vector2 position)
    {
        if (tracked && position != cursor) Aim(position);
    }

    void Cancel()
    {
        pose = false;
        Notice("placement canceled");
    }

    void Update()
    {
        var keys = Keyboard.current;
        var mouse = Mouse.current;
        var pad = Gamepad.current;
        if (keys != null)
        {
            if (keys.digit1Key.wasPressedThisFrame) { limb = 0; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (keys.digit2Key.wasPressedThisFrame) { limb = 1; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (keys.digit3Key.wasPressedThisFrame) { limb = 2; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (keys.digit4Key.wasPressedThisFrame) { limb = 3; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (keys.cKey.wasPressedThisFrame) close = !close;
            if (keys.rKey.wasPressedThisFrame) Restart();
            if (keys.leftBracketKey.wasPressedThisFrame) { route = (route + spec.routes.Length - 1) % spec.routes.Length; Build(); }
            if (keys.rightBracketKey.wasPressedThisFrame) { route = (route + 1) % spec.routes.Length; Build(); }
            if (keys.spaceKey.wasPressedThisFrame && Grab(limb)) pose = false;
            if (keys.xKey.wasPressedThisFrame) { Drop(limb); Notice($"{names[limb]} released"); }
            if (keys.escapeKey.wasPressedThisFrame) Cancel();
        }
        if (pad != null)
        {
            if (pad.dpad.down.wasPressedThisFrame) { limb = 0; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (pad.dpad.right.wasPressedThisFrame) { limb = 1; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (pad.dpad.left.wasPressedThisFrame) { limb = 2; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (pad.dpad.up.wasPressedThisFrame) { limb = 3; pose = true; aim = shown[limb]; Notice(names[limb]); }
            if (pad.buttonSouth.wasPressedThisFrame && Grab(limb)) pose = false;
            if (pad.buttonEast.wasPressedThisFrame)
            {
                if (pose) Cancel();
                else { Drop(limb); Notice($"{names[limb]} released"); }
            }
            if (pad.rightShoulder.wasPressedThisFrame) close = !close;
            if (pad.startButton.wasPressedThisFrame) Restart();
            var look = pad.rightStick.ReadValue();
            if (look.magnitude > 0.15f)
            {
                aim.x += look.x * Time.deltaTime * 2;
                aim.y += look.y * Time.deltaTime * 2;
            }
        }
        if (mouse != null)
        {
            var position = mouse.position.ReadValue();
            if (mouse.leftButton.wasPressedThisFrame) Press(position);
            Move(position);
            cursor = position;
            tracked = true;
            if (mouse.rightButton.wasPressedThisFrame)
            {
                if (pose) Cancel();
                else { Drop(limb); Notice($"{names[limb]} released"); }
            }
        }
        shift = Vector2.zero;
        if (keys != null)
            shift += new Vector2((keys.dKey.isPressed ? 1 : 0) - (keys.aKey.isPressed ? 1 : 0), (keys.wKey.isPressed ? 1 : 0) - (keys.sKey.isPressed ? 1 : 0));
        if (pad != null) shift += pad.leftStick.ReadValue();
        shift = Vector2.ClampMagnitude(shift, 1);
    }

    void FixedUpdate()
    {
        body.AddForce(new Vector3(shift.x, shift.y, 0) * spec.shift);
        // hips ride the wall at a real climber's distance, following overhangs
        var hug = body.position;
        hug.z = Surface(hug.y) + gap;
        body.position = hug;
        var holds = spec.routes[route].holds;
        var stretch = 0f;
        for (var i = 2; i < 4; i++)
            if (grips[i] >= 0 && points[grips[i]].y < Joint(i).y) stretch = Mathf.Max(stretch, Vector3.Distance(Joint(i), points[grips[i]]) / (Length(i) * 2));
        for (var i = 0; i < 4; i++)
        {
            var press = grips[i] < 0 && smeared[i];
            if (grips[i] < 0 && !press) continue;
            var hold = grips[i] >= 0 ? holds[grips[i]] : null;
            var point = press ? posts[i] : points[grips[i]];
            var offset = body.rotation * Rest(i);
            var velocity = body.linearVelocity + Vector3.Cross(body.angularVelocity, offset);
            var force = (point - body.position - offset) * spec.stiffness * (press ? 0.35f : 1) - velocity * spec.damping;
            if (rig != null)
            {
                // hands only pull; legs stop driving when straight or when they would stand up past a gripping hand
                var away = (Joint(i) - point).normalized;
                var reach = Vector3.Distance(Joint(i), point) / (Length(i) * 2);
                var push = Vector3.Dot(force, away);
                if (push > 0) force -= away * push * (i >= 2 ? 1 : Mathf.Clamp01(Mathf.Max(reach - 0.9f, stretch - 0.6f) / 0.15f));
                // a hand below its shoulder only steadies, until the arm runs out of length
                if (i >= 2 && point.y < Joint(i).y && force.y < 0) force.y *= Mathf.Lerp(0.15f, 1, Mathf.Clamp01((reach - 0.9f) / 0.1f));
            }
            body.AddForceAtPosition(force, body.position + offset);
            var load = new Vector2(-force.x, -force.y);
            var direction = press ? Vector2.down : new Vector2(hold.dx, hold.dy);
            var matchup = load.magnitude > 0.001f ? Mathf.Max(0, Vector2.Dot(load.normalized, direction)) : 1;
            var grip = press ? 260 : hold.kind == "jug" ? 1000 : hold.kind == "crimp" ? 600 : 480;
            var limit = grip * (0.5f + 0.5f * matchup) * (i < 2 ? 1.45f : press ? 0.55f : 1);
            slips[i] = load.magnitude > limit || Vector3.Distance(Joint(i), point) > Range(i) * 1.15f ? slips[i] + Time.fixedDeltaTime : Mathf.Max(0, slips[i] - Time.fixedDeltaTime * 2);
            if (slips[i] <= 0.16f) continue;
            Drop(i);
            slips[i] = 0;
            if (i == limb) Notice($"{names[i]} slipped: adjust your weight");
        }
        if (body.position.y < 0.62f) { Restart(); Notice("fell: try moving a foot before reaching"); }
        if (grips[2] == holds.Length - 1 || grips[3] == holds.Length - 1) won = true;
    }

    void Segment(Transform link, Vector3 start, Vector3 end)
    {
        link.position = (start + end) * 0.5f;
        link.up = (end - start).normalized;
        link.localScale = new Vector3(0.11f, Vector3.Distance(start, end) * 0.5f, 0.11f);
    }

    void LateUpdate()
    {
        pointer.position = aim;
        pointer.gameObject.SetActive(pose);
        var target = Nearest(aim);
        var pressable = aim.x >= minx && aim.x <= maxx && aim.y > 0.05f && aim.y <= top && Vector3.Distance(Joint(limb), new Vector3(aim.x, aim.y, Surface(aim.y) + 0.05f)) <= Range(limb);
        Tint(pointer.GetComponent<Renderer>(), target < 0 ? (pressable ? new Color(0.55f, 0.9f, 0.9f) : new Color(0.9f, 0.3f, 0.4f)) : Vector3.Distance(Joint(limb), points[target]) <= Range(limb) ? new Color(0.7f, 1, 0.3f) : new Color(1, 0.45f, 0.2f));
        for (var i = 0; i < spec.routes[route].holds.Length; i++)
        {
            var hold = spec.routes[route].holds[i];
            var color = hold.kind == "jug" ? new Color(0.98f, 0.56f, 0.17f) : hold.kind == "crimp" ? new Color(0.9f, 0.3f, 0.45f) : new Color(0.6f, 0.45f, 0.9f);
            if (Vector3.Distance(Joint(limb), points[i]) <= Range(limb)) color = Color.Lerp(color, new Color(0.4f, 1, 0.4f), 0.35f);
            Tint(marks[i], color);
        }
        for (var i = 0; i < 4; i++)
        {
            var start = Joint(i);
            var end = pose && i == limb ? start + Vector3.ClampMagnitude(aim - start, Range(i)) : grips[i] >= 0 ? points[grips[i]] : smeared[i] ? posts[i] : start + new Vector3(i % 2 == 0 ? -0.25f : 0.25f, i >= 2 ? 0.32f : -0.45f, -0.18f);
            shown[i] = Vector3.Lerp(shown[i], end, 1 - Mathf.Exp(-Time.deltaTime * 18));
            var middle = Bend(start, shown[i], i);
            bends[i].position = middle;
            tips[i].position = shown[i];
            tips[i].localScale = smeared[i] && grips[i] < 0 ? new Vector3(0.24f, 0.24f, 0.09f) : Vector3.one * 0.21f;
            Tint(ends[i], i == limb ? new Color(1, 0.9f, 0.25f) : smeared[i] ? new Color(0.55f, 0.9f, 0.9f) : grips[i] < 0 ? new Color(1, 0.35f, 0.35f) : i >= 2 ? new Color(0.95f, 0.83f, 0.66f) : new Color(0.16f, 0.57f, 0.7f));
            Segment(links[i, 0], start, middle);
            Segment(links[i, 1], middle, shown[i]);
        }
        avatar.enabled = false;
        var proxy = rig == null;
        for (var i = 0; i < 4; i++)
        {
            links[i, 0].gameObject.SetActive(proxy);
            links[i, 1].gameObject.SetActive(proxy);
            bends[i].gameObject.SetActive(proxy);
        }
        Pose();
        if (rig != null) rig.gameObject.SetActive(!close);
        var center = body.position + (close ? new Vector3(0, 0.28f, 0.35f) : new Vector3(0, 0.22f, 5.8f));
        view.transform.position = Vector3.Lerp(view.transform.position, center, 0.13f);
        view.transform.LookAt(close ? aim : body.position + new Vector3(0, 0.25f, -0.3f));
    }

    void Pose()
    {
        if (rig == null) return;
        rig.rotation = body.rotation * facing;
        rig.position = body.position + body.rotation * rigoffset;
        for (var i = 0; i < joints.Length; i++) joints[i].localRotation = binds[i];
        for (var i = 0; i < 4; i++)
        {
            var upper = bones[i, 0];
            var lower = bones[i, 1];
            var tip = bones[i, 2];
            var end = shown[i];
            if (i >= 2 && clavs[i - 2] != null)
            {
                var clav = clavs[i - 2];
                var want = end - clav.position;
                var over = Mathf.Clamp01((want.magnitude - reallen[i] * 2) / reallen[i]);
                var swing = Quaternion.FromToRotation(upper.position - clav.position, want);
                clav.rotation = Quaternion.Slerp(Quaternion.identity, swing, over * 0.35f) * clav.rotation;
            }
            var start = upper.position;
            var mid = Bend(start, end, i, reallen[i]);
            upper.rotation = Quaternion.FromToRotation(lower.position - start, mid - start) * upper.rotation;
            lower.rotation = Quaternion.FromToRotation(tip.position - lower.position, end - lower.position) * lower.rotation;
            if (i < 2)
            {
                var toe = toes[i];
                if (toe != null && (grips[i] >= 0 || smeared[i]))
                    tip.rotation = Quaternion.FromToRotation(toe.position - tip.position, rig.forward) * tip.rotation;
            }
            else tip.rotation = lower.rotation * tiprel[i];
        }
    }

    void OnGUI()
    {
        if (spec == null) return;
        GUI.color = Color.white;
        var style = new GUIStyle(GUI.skin.label) { fontSize = 19 };
        var status = $"rougarou / {spec.routes[route].name} / {(close ? "first" : "third")}\n{(pose ? $"moving {names[limb]}" : "click a hand or foot")}  |  move mouse to aim  |  click a hold or bare wall to place\nwasd shifts hips  |  esc cancels  |  x releases  |  c camera  |  [ ] route  |  r retry\n{(won ? "sent! " : Time.unscaledTime < until ? hint : "")}";
        GUI.Label(new Rect(18, 18, 900, 132), status, style);
        for (var i = 0; i < 4; i++)
        {
            var point = view.WorldToScreenPoint(shown[i]);
            if (point.z <= 0) continue;
            GUI.Label(new Rect(point.x + 12, Screen.height - point.y - 12, 45, 30), labels[i], style);
        }
    }

#if UNITY_EDITOR
    (Vector3, Quaternion, Vector3, Vector3, int[], bool[], float[], Vector3[], Vector3[], bool) Snapshot() =>
        (body.position, body.rotation, body.linearVelocity, body.angularVelocity, (int[])grips.Clone(), (bool[])smeared.Clone(), (float[])slips.Clone(), (Vector3[])posts.Clone(), (Vector3[])shown.Clone(), won);

    void Restore((Vector3, Quaternion, Vector3, Vector3, int[], bool[], float[], Vector3[], Vector3[], bool) state)
    {
        body.position = state.Item1;
        body.rotation = state.Item2;
        body.linearVelocity = state.Item3;
        body.angularVelocity = state.Item4;
        state.Item5.CopyTo(grips, 0);
        state.Item6.CopyTo(smeared, 0);
        state.Item7.CopyTo(slips, 0);
        state.Item8.CopyTo(posts, 0);
        state.Item9.CopyTo(shown, 0);
        won = state.Item10;
    }

    public static void Probe()
    {
        var mode = Physics.simulationMode;
        Physics.simulationMode = SimulationMode.Script;
        var test = new GameObject("probe").AddComponent<Main>();
        test.Start();
        test.view.pixelRect = new Rect(0, 0, 1280, 800);
        test.LateUpdate();
        var left = test.view.WorldToScreenPoint(test.shown[2]);
        var start = new Vector2(left.x, left.y);
        if (test.Pick(start) != 2) throw new Exception("left hand must be selectable by its tip");
        test.limb = 3;
        test.Press(start);
        if (test.limb != 2 || !test.pose) throw new Exception("clicking the left hand must select it");
        var high = test.view.WorldToScreenPoint(test.points[4]);
        var end = new Vector2(high.x, high.y);
        test.Move(end);
        if (!test.pose || Vector2.Distance(test.aim, test.points[4]) > 0.2f)
            throw new Exception("cursor movement must aim the selected fingertip");
        var grasp = test.Joint(2) + new Vector3(-0.1f, 0.6f, -0.2f).normalized * test.Range(2) * 0.9f;
        var elbow = test.Bend(test.Joint(2), grasp, 2);
        if (Mathf.Abs(Vector3.Distance(test.Joint(2), elbow) - test.Length(2)) > 0.02f || Mathf.Abs(Vector3.Distance(elbow, grasp) - test.Length(2)) > 0.02f)
            throw new Exception("elbow must join two fixed-length arm segments");
        var knee = test.Bend(test.Joint(0), test.points[0], 0);
        if (Mathf.Abs(Vector3.Distance(test.Joint(0), knee) - test.Length(0)) > 0.02f || Mathf.Abs(Vector3.Distance(knee, test.points[0]) - test.Length(0)) > 0.02f)
            throw new Exception("knee must join two fixed-length leg segments");
        test.Press(end);
        if (test.pose || test.grips[2] != 4) throw new Exception("clicking a reachable hold must place the left hand");
        test.Restart();
        test.Press(start);
        var far = test.view.WorldToScreenPoint(test.points[5]);
        var distant = new Vector2(far.x, far.y);
        test.Move(distant);
        test.Press(distant);
        if (!test.pose || test.grips[2] != 2 || !test.hint.Contains("cannot reach"))
            throw new Exception("out-of-reach click must explain the failure without releasing the hand");
        test.Cancel();
        if (test.pose) throw new Exception("cancel must leave the previous grip intact");
        test.Restart();
        var foot = test.view.WorldToScreenPoint(test.shown[0]);
        test.Press(new Vector2(foot.x, foot.y));
        if (!test.pose || test.limb != 0) throw new Exception("left foot must be selectable by its tip");
        var shared = test.view.WorldToScreenPoint(test.points[2]);
        test.Move(new Vector2(shared.x, shared.y));
        test.Press(new Vector2(shared.x, shared.y));
        if (test.pose || test.grips[0] != 2)
            throw new Exception("a foot must be placeable on an occupied handhold");
        test.Restart();
        test.Press(new Vector2(foot.x, foot.y));
        var bare = new Vector3(-0.3f, 0.95f, test.points[0].z);
        test.aim = bare;
        if (!test.Grab(0) || !test.smeared[0] || test.grips[0] != -1)
            throw new Exception("a foot must smear bare wall within reach");
        for (var frame = 0; frame < 90; frame++)
        {
            test.FixedUpdate();
            Physics.defaultPhysicsScene.Simulate(Time.fixedDeltaTime);
        }
        if (!test.smeared[0]) throw new Exception("a smear must hold steady body weight");
        test.Drop(0);
        if (test.smeared[0]) throw new Exception("release must clear a smear");
        test.Restart();
        for (var route = 0; route < 3; route++)
        {
            if (route > 0)
            {
                test.route = route;
                test.Build();
            }
            for (var frame = 0; frame < 180; frame++)
            {
                test.FixedUpdate();
                Physics.defaultPhysicsScene.Simulate(Time.fixedDeltaTime);
            }
            Debug.Log($"{test.spec.routes[route].name}: height={test.body.position.y:F2} grips={string.Join(",", test.grips)}");
            if (test.body.position.y <= 0 || Array.Exists(test.grips, grip => grip < 0))
                throw new Exception("initial grips must survive a static hang");
            test.aim = test.points[test.spec.routes[route].holds.Length - 1];
            test.Grab(2);
            if (test.grips[2] == test.spec.routes[route].holds.Length - 1)
                throw new Exception("unreachable grip must not attach");
            var arm = route == 2 ? 3 : 2;
            test.aim = test.points[4];
            test.Grab(arm);
            if (test.grips[arm] != 4)
                throw new Exception("reachable grip must attach");
            for (var frame = 0; frame < 90; frame++)
            {
                test.FixedUpdate();
                Physics.defaultPhysicsScene.Simulate(Time.fixedDeltaTime);
            }
            Debug.Log($"{test.spec.routes[route].name}: reach height={test.body.position.y:F2} grips={string.Join(",", test.grips)}");
            if (test.grips[arm] != 4)
                throw new Exception("first reach must remain attached");
            for (var i = 0; i < 4; i++) test.grips[i] = -1;
            for (var frame = 0; frame < 120; frame++)
            {
                test.FixedUpdate();
                Physics.defaultPhysicsScene.Simulate(Time.fixedDeltaTime);
            }
            if (test.grips[0] != 0 || test.grips[1] != 1 || test.grips[2] != 2 || test.grips[3] != 3)
                throw new Exception("fall must reset to starting holds");
        }
        // greedy climber: try every limb on every hold, keep the move that progresses most without losing a hand
        for (var route = 0; route < test.spec.routes.Length; route++)
        {
            test.route = route;
            test.Build();
            var across = route == 2;
            for (var step = 0; step < 24 && !test.won; step++)
            {
                var saved = test.Snapshot();
                float Score()
                {
                    var total = test.won ? 1000 : 0f;
                    for (var i = 0; i < 4; i++)
                    {
                        if (test.grips[i] < 0) { total -= 0.3f; continue; }
                        var point = test.points[test.grips[i]];
                        total += (across ? point.x : point.y) * (i >= 2 ? 1 : 0.6f);
                    }
                    return total;
                }
                void Settle(int frames)
                {
                    for (var frame = 0; frame < frames; frame++)
                    {
                        test.FixedUpdate();
                        Physics.defaultPhysicsScene.Simulate(Time.fixedDeltaTime);
                    }
                    test.shift = Vector2.zero;
                }
                // limbs 0-3 place on a hold; 4-7 shift the hips left, right, up, down then try the best hand move from there
                var shifts = new[] { Vector2.left, Vector2.right, Vector2.up, Vector2.down };
                var best = Score() + 0.01f;
                var choice = (-1, -1, -1);
                for (var lean = -1; lean < 4; lean++)
                    for (var limb = 0; limb < 4; limb++)
                        for (var target = 0; target < test.spec.routes[route].holds.Length; target++)
                        {
                            if (test.grips[limb] == target) continue;
                            test.Restore(saved);
                            if (lean >= 0)
                            {
                                test.shift = shifts[lean] * 0.6f;
                                Settle(30);
                                if (test.grips[2] < 0 || test.grips[3] < 0) continue;
                            }
                            test.aim = test.points[target];
                            if (!test.Grab(limb)) continue;
                            Settle(45);
                            // a foot may swing free after a high step, but both hands must still be on
                            if (test.grips[2] < 0 || test.grips[3] < 0) continue;
                            var score = Score();
                            if (score > best) { best = score; choice = (lean, limb, target); }
                        }
                test.Restore(saved);
                if (choice.Item2 < 0) break;
                if (choice.Item1 >= 0)
                {
                    test.shift = shifts[choice.Item1] * 0.6f;
                    Settle(30);
                }
                test.aim = test.points[choice.Item3];
                test.Grab(choice.Item2);
                Settle(45);
                Debug.Log($"{test.spec.routes[route].name}: {(choice.Item1 >= 0 ? $"lean {shifts[choice.Item1]}, " : "")}move {names[choice.Item2]} to {choice.Item3} height={test.body.position.y:F2} grips={string.Join(",", test.grips)}");
            }
            if (!test.won) throw new Exception($"{test.spec.routes[route].name} must be solvable");
        }
        Debug.Log("rougarou unity probe passed");
        Physics.simulationMode = mode;
    }
#endif
}
