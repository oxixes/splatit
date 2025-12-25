import {Home, Users, Settings, Activity, Gamepad2} from "lucide-react"
import {
    Sidebar,
    SidebarContent,
    SidebarGroup,
    SidebarGroupContent,
    SidebarGroupLabel,
    SidebarMenu,
    SidebarMenuButton,
    SidebarMenuItem,
    SidebarHeader,
    SidebarFooter,
} from "@/components/ui/sidebar"
import {NavLink} from "react-router";

// Menu items
const mainItems = [
    {
        title: "Dashboard",
        url: "/",
        icon: Home,
    },
    {
        title: "Lobbies",
        url: "/lobbies",
        icon: Gamepad2,
    },
    {
        title: "Players",
        url: "/players",
        icon: Users,
    },
]

const managementItems = [
    {
        title: "Server Status",
        url: "/server-status",
        icon: Activity,
    },
]

const systemItems = [
    {
        title: "Settings",
        url: "/settings",
        icon: Settings,
    },
]

export function AppSidebar() {
    return (
        <Sidebar>
            <SidebarHeader>
                <div className="flex items-center gap-2 px-4 py-2">
                    <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-primary text-primary-foreground">
                        <span className="text-lg font-bold">🦑</span>
                    </div>
                    <div className="flex flex-col">
                        <span className="text-lg font-bold">SplatIt</span>
                        <span className="text-xs text-muted-foreground">Server Manager</span>
                    </div>
                </div>
            </SidebarHeader>
            <SidebarContent>
                <SidebarGroup>
                    <SidebarGroupLabel>Main</SidebarGroupLabel>
                    <SidebarGroupContent>
                        <SidebarMenu>
                            {mainItems.map((item) => (
                                <SidebarMenuItem key={item.title}>
                                    <SidebarMenuButton asChild>
                                        <NavLink to={item.url} end>
                                            <item.icon />
                                            <span>{item.title}</span>
                                        </NavLink>
                                    </SidebarMenuButton>
                                </SidebarMenuItem>
                            ))}
                        </SidebarMenu>
                    </SidebarGroupContent>
                </SidebarGroup>

                <SidebarGroup>
                    <SidebarGroupLabel>System</SidebarGroupLabel>
                    <SidebarGroupContent>
                        <SidebarMenu>
                            {[...managementItems, ...systemItems].map((item) => (
                                <SidebarMenuItem key={item.title}>
                                    <SidebarMenuButton asChild>
                                        <NavLink to={item.url}>
                                            <item.icon />
                                            <span>{item.title}</span>
                                        </NavLink>
                                    </SidebarMenuButton>
                                </SidebarMenuItem>
                            ))}
                        </SidebarMenu>
                    </SidebarGroupContent>
                </SidebarGroup>
            </SidebarContent>
            <SidebarFooter>
                <div className="px-4 py-2 text-xs text-muted-foreground">
                    {__APP_VERSION__} • <a href="https://github.com/oxixes/splatoon_server_cpp" className="underline" target="_blank">GitHub</a>
                </div>
            </SidebarFooter>
        </Sidebar>
    )
}