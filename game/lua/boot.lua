log.info("Game started!")

local world = stormkit.world
local player_texture = stormkit.resources:load_image("./textures/player_tileset.png")

local function make_player()
    local sprite_rect = math.fbounding_rect.new(0, 0, 16, 22)
    local player = stormkit.make_sprite(world, player_texture, sprite_rect)

    world:add_component(player, stormkit.debug_name_component.new("player"))

    return player
end

local player = make_player()

local test = world:make_entity()
world:add_component(test, stormkit.transform_component.new())

local transform = world:get_component(player, "TransformComponent")

log.info("Player created with id {} {}", player, transform.position)
