extends SceneTree

func _initialize() -> void:
	call_deferred("check")

func check() -> void:
	var scene = load("res://main.tscn").instantiate()
	root.add_child(scene)
	for route in 3:
		if route > 0:
			scene.route = route
			scene.build()
		for frame in 180:
			await physics_frame
		print("%s: height=%.2f grips=%s" % [scene.data["routes"][route]["name"], scene.body.global_position.y, str(scene.grips)])
		assert(scene.body.global_position.y > 0.0, "initial stance must remain above the mat")
		assert(scene.grips.count(-1) == 0, "initial grips must survive a static hang")
		var high: Vector3 = scene.holds[scene.holds.size() - 1]["point"]
		scene.aim = high
		scene.attach(2)
		assert(scene.grips[2] != scene.holds.size() - 1, "unreachable grip must not attach")
		var arm := 3 if route == 2 else 2
		scene.aim = scene.holds[4]["point"]
		scene.attach(arm)
		assert(scene.grips[arm] == 4, "reachable grip must attach")
		for frame in 90:
			await physics_frame
		print("%s: reach height=%.2f grips=%s" % [scene.data["routes"][route]["name"], scene.body.global_position.y, str(scene.grips)])
		assert(scene.body.global_position.y > 0.0, "climber must survive first reach")
		assert(scene.grips[arm] == 4, "first reach must remain attached after settling")
		scene.grips = [-1, -1, -1, -1]
		for frame in 120:
			await physics_frame
		assert(scene.grips == [0, 1, 2, 3], "fall must reset to starting holds")
	scene.route = 0
	scene.build()
	var moves := [[2, 4], [0, 2], [3, 5], [1, 3], [2, 6], [0, 4], [1, 5], [3, 7]]
	for move in moves:
		scene.aim = scene.holds[move[1]]["point"]
		scene.attach(move[0])
		for frame in 45:
			await physics_frame
		print("sequence: target=%s height=%.2f grips=%s" % [str(move), scene.body.global_position.y, str(scene.grips)])
	assert(scene.won, "vertical problem must be solvable with deliberate footwork")
	var paths := [
		[[2, 4], [0, 2], [1, 3], [2, 6], [0, 4], [1, 4], [2, 7]],
		[[3, 4], [1, 3], [0, 2], [1, 5], [2, 3], [0, 3], [3, 6], [1, 7], [2, 4], [3, 8]]
	]
	for route in 2:
		scene.route = route + 1
		scene.build()
		for move in paths[route]:
			if scene.route == 2 and move[1] >= 5:
				for frame in 30:
					scene.body.apply_central_force(Vector3(500, 0, 0))
					await physics_frame
			scene.aim = scene.holds[move[1]]["point"]
			print("%s: try=%s distance=%.2f x=%.2f" % [scene.data["routes"][scene.route]["name"], str(move), scene.joint(move[0]).distance_to(scene.aim), scene.body.global_position.x])
			scene.attach(move[0])
			for frame in 45:
				await physics_frame
			print("%s: target=%s height=%.2f grips=%s" % [scene.data["routes"][scene.route]["name"], str(move), scene.body.global_position.y, str(scene.grips)])
		print("%s: sent=%s" % [scene.data["routes"][scene.route]["name"], str(scene.won)])
		assert(scene.won, "each authored problem must be solvable")
	quit()
