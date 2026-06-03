import {type RouteConfig, index, layout, route} from "@react-router/dev/routes";

export default [
    route("login", "routes/login.tsx"),
    layout('layout.tsx', [
        index("routes/home.tsx"),
        route("lobbies", "routes/lobbies.tsx"),
        route("players", "routes/players.tsx"),
        route("devices", "routes/devices.tsx"),
        route("server-status", "routes/server-status.tsx"),
        route("settings", "routes/settings.tsx"),
        route("settings/splatfest/:id", "routes/splatfest-editor.tsx"),
        route("settings/map-rotation", "routes/map-rotation.tsx"),
    ])
] satisfies RouteConfig;
