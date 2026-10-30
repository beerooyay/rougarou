extends Node3D

var data: Dictionary
var route := 0
var limb := 2
var close := false
var grips: Array = [-1, -1, -1, -1]
var slips: Array = [0.0, 0.0, 0.0, 0.0]
var holds: Array = []
var links: Array = []
var aim := Vector3(0.4, 2.15, 0.15)
var slope := 0.0
var won := false
var body: RigidBody3D
var camera: Camera3D
var text: Label
var pointer: MeshInstance3D
var avatar: MeshInstance3D

func _ready() -> void:
	var file := FileAccess.open("res://routes.json", FileAccess.READ)
	if file == null:
		push_error("missing shared/routes.json")
		return
	data = JSON.parse_string(file.get_as_text())
	environment()
	build()

func material(color: Color) -> StandardMaterial3D:
	var result := StandardMaterial3D.new()
	result.albedo_color = color
	result.roughness = 0.9
	return result

func box(parent: Node3D, size: Vector3, color: Color, solid := false) -> Node3D:
	var node: Node3D
	if solid:
		node = StaticBody3D.new()
		var collision := CollisionShape3D.new()
		var shape := BoxShape3D.new()
		shape.size = size
		collision.shape = shape
		node.add_child(collision)
	else:
		node = Node3D.new()
	parent.add_child(node)
	var mesh := MeshInstance3D.new()
	var shape := BoxMesh.new()
	shape.size = size
	mesh.mesh = shape
	mesh.material_override = material(color)
	node.add_child(mesh)
	return node

func environment() -> void:
	var world := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color(0.065, 0.08, 0.11)
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color = Color(0.8, 0.84, 1.0)
	settings.ambient_light_energy = 0.7
	world.environment = settings
	add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35, -25, 0)
	light.light_energy = 1.6
	add_child(light)
	var floor := box(self, Vector3(12, 0.25, 10), Color(0.15, 0.19, 0.22), true)
	floor.position = Vector3(0, -0.18, 1.5)
	var hud := CanvasLayer.new()
	add_child(hud)
	text = Label.new()
	text.position = Vector2(18, 18)
	text.add_theme_font_size_override("font_size", 18)
	hud.add_child(text)
	camera = Camera3D.new()
	camera.fov = 56.0
	add_child(camera)
	camera.current = true

func surface(y: float) -> float:
	return 0.15 + (y - 1.45) * slope

func start() -> void:
	body.global_position = Vector3(0, 1.1, 0.85)
	body.linear_velocity = Vector3.ZERO
	body.angular_velocity = Vector3.ZERO
	body.rotation = Vector3.ZERO
	grips = [0, 1, 2, 3]
	slips = [0.0, 0.0, 0.0, 0.0]
	limb = 2
	won = false
	aim = holds[4]["point"]

func build() -> void:
	for node in get_children():
		if node.is_in_group("route"):
			node.queue_free()
	holds.clear()
	links.clear()
	slope = 0.25 if route == 1 else 0.0
	var wall := box(self, Vector3(5.4, 4.7, 0.3), Color(0.28, 0.31, 0.34), true)
	wall.add_to_group("route")
	wall.position = Vector3(0.7 if route == 2 else 0, 2.15, surface(2.15) - 0.14)
	wall.rotation.x = atan(slope)
	for entry in data["routes"][route]["holds"]:
		var point := Vector3(entry["x"], entry["y"], surface(entry["y"]) + 0.05)
		var mesh := MeshInstance3D.new()
		var shape := SphereMesh.new()
		shape.radius = 0.135 if entry["kind"] != "sloper" else 0.18
		shape.height = shape.radius * 2.0
		mesh.mesh = shape
		mesh.material_override = material(Color(0.98, 0.56, 0.17) if entry["kind"] == "jug" else Color(0.9, 0.3, 0.45) if entry["kind"] == "crimp" else Color(0.6, 0.45, 0.9))
		mesh.position = point
		mesh.add_to_group("route")
		add_child(mesh)
		holds.append({"point": point, "type": entry["kind"], "dir": Vector2(entry["dx"], entry["dy"])})
	body = RigidBody3D.new()
	body.mass = data["mass"]
	body.linear_damp = 0.55
	body.angular_damp = 6.0
	body.axis_lock_linear_z = true
	body.axis_lock_angular_x = true
	body.axis_lock_angular_y = true
	body.add_to_group("route")
	add_child(body)
	var collision := CollisionShape3D.new()
	var shape := CapsuleShape3D.new()
	shape.radius = 0.22
	shape.height = 0.9
	collision.shape = shape
	body.add_child(collision)
	var torso := MeshInstance3D.new()
	var model := CapsuleMesh.new()
	model.radius = 0.23
	model.height = 0.85
	torso.mesh = model
	torso.material_override = material(Color(0.09, 0.8, 0.7))
	body.add_child(torso)
	avatar = torso
	for index in 4:
		var link := MeshInstance3D.new()
		var limbmesh := CylinderMesh.new()
		limbmesh.top_radius = 0.055
		limbmesh.bottom_radius = 0.065
		limbmesh.height = 1.0
		link.mesh = limbmesh
		link.material_override = material(Color(0.95, 0.83, 0.66) if index >= 2 else Color(0.16, 0.57, 0.7))
		link.add_to_group("route")
		add_child(link)
		links.append(link)
	pointer = MeshInstance3D.new()
	var ball := SphereMesh.new()
	ball.radius = 0.08
	ball.height = 0.16
	pointer.mesh = ball
	pointer.material_override = material(Color(1, 1, 0.65))
	pointer.add_to_group("route")
	add_child(pointer)
	start()

func joint(index: int) -> Vector3:
	var side := -1.0 if index % 2 == 0 else 1.0
	var offset := Vector3(side * (0.26 if index >= 2 else 0.17), 0.25 if index >= 2 else -0.24, -0.16)
	return body.global_position + body.global_basis * offset

func rest(index: int) -> Vector3:
	var side := -1.0 if index % 2 == 0 else 1.0
	return Vector3(side * (0.4 if index >= 2 else 0.3), 0.35 if index >= 2 else -0.55, -0.65)

func attach(index: int) -> void:
	var candidate := -1
	var distance := 0.35
	for i in holds.size():
		var point: Vector3 = holds[i]["point"]
		var delta := Vector2(point.x - aim.x, point.y - aim.y).length()
		if delta < distance and joint(index).distance_to(point) <= data["reach"]:
			candidate = i
			distance = delta
	if candidate >= 0:
		grips[index] = candidate
		slips[index] = 0.0

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_1: limb = 0
			KEY_2: limb = 1
			KEY_3: limb = 2
			KEY_4: limb = 3
			KEY_C: close = not close
			KEY_R: start()
			KEY_BRACKETLEFT:
				route = posmod(route - 1, data["routes"].size())
				build()
			KEY_BRACKETRIGHT:
				route = (route + 1) % data["routes"].size()
				build()
			KEY_SPACE: attach(limb)
			KEY_X: grips[limb] = -1
	if event is InputEventMouseMotion:
		var origin := camera.project_ray_origin(event.position)
		var ray := camera.project_ray_normal(event.position)
		var denom := ray.z - ray.y * slope
		if absf(denom) > 0.001:
			var travel := (surface(origin.y) - origin.z) / denom
			if travel > 0:
				aim = origin + ray * travel
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			attach(limb)
		if event.button_index == MOUSE_BUTTON_RIGHT:
			grips[limb] = -1
	if event is InputEventJoypadButton and event.pressed:
		match event.button_index:
			JOY_BUTTON_DPAD_DOWN: limb = 0
			JOY_BUTTON_DPAD_RIGHT: limb = 1
			JOY_BUTTON_DPAD_LEFT: limb = 2
			JOY_BUTTON_DPAD_UP: limb = 3
			JOY_BUTTON_A: attach(limb)
			JOY_BUTTON_B: grips[limb] = -1
			JOY_BUTTON_RIGHT_SHOULDER: close = not close
			JOY_BUTTON_START: start()

func _physics_process(delta: float) -> void:
	if body == null:
		return
	var shift := Vector2(float(Input.is_key_pressed(KEY_D)) - float(Input.is_key_pressed(KEY_A)), float(Input.is_key_pressed(KEY_W)) - float(Input.is_key_pressed(KEY_S)))
	var pads := Input.get_connected_joypads()
	if not pads.is_empty():
		var pad: int = pads[0]
		shift += Vector2(Input.get_joy_axis(pad, JOY_AXIS_LEFT_X), -Input.get_joy_axis(pad, JOY_AXIS_LEFT_Y))
		var look := Vector2(Input.get_joy_axis(pad, JOY_AXIS_RIGHT_X), -Input.get_joy_axis(pad, JOY_AXIS_RIGHT_Y))
		if look.length() > 0.15:
			aim.x += look.x * delta * 2.0
			aim.y += look.y * delta * 2.0
	if shift.length() > 1.0:
		shift = shift.normalized()
	body.apply_central_force(Vector3(shift.x, shift.y, 0) * float(data["shift"]))
	for i in 4:
		if grips[i] < 0:
			continue
		var hold: Dictionary = holds[grips[i]]
		var offset := body.global_basis * rest(i)
		var point: Vector3 = hold["point"]
		var velocity := body.linear_velocity + body.angular_velocity.cross(offset)
		var force := (point - body.global_position - offset) * float(data["stiffness"]) - velocity * float(data["damping"])
		body.apply_force(force, offset)
		var load := Vector2(-force.x, -force.y)
		var direction: Vector2 = hold["dir"]
		var matchup := maxf(0.0, load.normalized().dot(direction)) if load.length() > 0.001 else 1.0
		var limit := (1000.0 if hold["type"] == "jug" else 600.0 if hold["type"] == "crimp" else 480.0) * (0.5 + 0.5 * matchup) * (1.45 if i < 2 else 1.0)
		if load.length() > limit or joint(i).distance_to(point) > float(data["reach"]) * 1.15:
			slips[i] += delta
		else:
			slips[i] = maxf(0.0, slips[i] - delta * 2.0)
		if slips[i] > 0.16:
			grips[i] = -1
			slips[i] = 0.0
	if body.global_position.y < 0.62:
		start()
	if not won and (grips[2] == holds.size() - 1 or grips[3] == holds.size() - 1):
		won = true

func _process(_delta: float) -> void:
	if body == null:
		return
	pointer.position = aim
	for i in 4:
		var startpoint := joint(i)
		var endpoint: Vector3 = holds[grips[i]]["point"] if grips[i] >= 0 else startpoint + Vector3((-0.25 if i % 2 == 0 else 0.25), 0.32 if i >= 2 else -0.45, -0.18)
		var link: MeshInstance3D = links[i]
		link.global_position = (startpoint + endpoint) * 0.5
		link.look_at(endpoint, Vector3.FORWARD)
		link.rotate_object_local(Vector3.RIGHT, PI * 0.5)
		link.scale = Vector3(1, startpoint.distance_to(endpoint), 1)
	avatar.visible = not close
	var target := body.global_position + (Vector3(0, 0.28, 0.35) if close else Vector3(0, 0.22, 5.8))
	camera.global_position = camera.global_position.lerp(target, 0.13)
	camera.look_at(aim if close else body.global_position + Vector3(0, 0.25, -0.3))
	text.text = "rougarou / %s / %s\n1-4 select limb   click/space grip   right click/x release\nwasd shift hips   c close view   [ ] route   r reset\n%s%s" % [
		data["routes"][route]["name"],
		"first" if close else "third",
		"sent! " if won else "",
		"feet %s %s  hands %s %s" % [grips[0], grips[1], grips[2], grips[3]]
	]
