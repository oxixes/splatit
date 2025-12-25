import { SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar"
import { AppSidebar } from "@/components/app-sidebar"
import { Outlet } from "react-router";
import { ThemeProvider } from "@/components/theme-provider"

export default function Layout() {
    return (
        <ThemeProvider defaultTheme="dark" storageKey="vite-ui-theme">
            <SidebarProvider>
                <AppSidebar />
                <main className="w-full">
                    <SidebarTrigger />
                    <div className="w-full p-3">
                        <Outlet />
                    </div>
                </main>
            </SidebarProvider>
        </ThemeProvider>
    )
}