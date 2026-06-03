import { SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar"
import { AppSidebar } from "@/components/app-sidebar"
import { Navigate, Outlet, useLocation } from "react-router";
import { ThemeProvider } from "@/components/theme-provider"
import { useAuth } from "~/contexts/AuthContext";
import { SplatoonStatsProvider } from "~/contexts/SplatoonStatsContext";

export default function Layout() {
    const { user, loading } = useAuth();
    const location = useLocation();

    if (loading) {
        return null;
    }

    if (!user) {
        return <Navigate to="/login" replace state={{ from: location.pathname }} />;
    }

    return (
        <ThemeProvider defaultTheme="dark" storageKey="vite-ui-theme">
            <SplatoonStatsProvider>
                <SidebarProvider>
                    <AppSidebar />
                    <main className="w-full">
                        <SidebarTrigger />
                        <div className="w-full p-3">
                            <Outlet />
                        </div>
                    </main>
                </SidebarProvider>
            </SplatoonStatsProvider>
        </ThemeProvider>
    )
}
