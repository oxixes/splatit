import {Home, Users, Settings, Activity, Gamepad2, Info, Smartphone, LogOut} from "lucide-react"
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
import { useConfig } from "~/contexts/AppConfigContext";
import { useAuth } from "~/contexts/AuthContext";
import { Button } from "~/components/ui/button";
import {
    Tooltip,
    TooltipContent,
    TooltipProvider,
    TooltipTrigger,
} from "@/components/ui/tooltip"

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
    {
        title: "Devices",
        url: "/devices",
        icon: Smartphone,
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
    const { config } = useConfig();
    const { user, logout } = useAuth();

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
                <div className="px-4 py-2 text-xs text-muted-foreground space-y-1">
                    <div className="flex items-center justify-between gap-2">
                        <span className="truncate">{user?.username}</span>
                        <Button variant="ghost" size="icon" className="h-7 w-7" onClick={logout} title="Sign out">
                            <LogOut className="h-4 w-4" />
                        </Button>
                    </div>
                    <div className="flex items-center gap-1">
                        {__APP_VERSION__} • <a href="https://github.com/oxixes/splatit" className="underline" target="_blank">GitHub</a>
                        <TooltipProvider>
                            <Tooltip>
                                <TooltipTrigger asChild>
                                    <Info className="h-3 w-3 cursor-help" />
                                </TooltipTrigger>
                                <TooltipContent side="top" className="max-w-xs">
                                    <div className="space-y-2">
                                        <div>
                                            <p className="font-semibold text-xs">API URL:</p>
                                            <p className="text-xs break-all">{config.apiUrl}</p>
                                        </div>
                                        {config.compatibleVersions.length > 0 && (
                                            <div>
                                                <p className="font-semibold text-xs">Compatible Versions:</p>
                                                <p className="text-xs">{config.compatibleVersions.join(", ")}</p>
                                            </div>
                                        )}
                                    </div>
                                </TooltipContent>
                            </Tooltip>
                        </TooltipProvider>
                    </div>
                </div>
            </SidebarFooter>
        </Sidebar>
    )
}
