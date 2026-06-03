import { createContext, useCallback, useContext, useEffect, useState, type ReactNode } from "react";
import { useConfig } from "~/contexts/AppConfigContext";

export const AUTH_TOKEN_KEY = "splatit_auth_token";

interface AuthUser {
  pid: number;
  username: string;
  isAdmin: boolean;
}

interface AuthContextType {
  user: AuthUser | null;
  token: string | null;
  loading: boolean;
  login: (username: string, password: string) => Promise<void>;
  logout: () => void;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

function parseJwt(token: string): Record<string, unknown> | null {
  try {
    const parts = token.split(".");
    if (parts.length !== 3) return null;
    const base64 = parts[1].replace(/-/g, "+").replace(/_/g, "/");
    const padded = base64.padEnd(base64.length + ((4 - (base64.length % 4)) % 4), "=");
    return JSON.parse(atob(padded));
  } catch {
    return null;
  }
}

function getUserFromToken(token: string): AuthUser | null {
  const payload = parseJwt(token);
  if (!payload) return null;

  if (typeof payload.exp === "number" && payload.exp <= Math.floor(Date.now() / 1000)) {
    return null;
  }

  if (payload.is_admin !== true || typeof payload.username !== "string" || typeof payload.sub !== "number") {
    return null;
  }

  return {
    pid: payload.sub,
    username: payload.username,
    isAdmin: payload.is_admin,
  };
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const { config, loading: configLoading } = useConfig();
  const [user, setUser] = useState<AuthUser | null>(null);
  const [token, setToken] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    if (configLoading) return;

    const storedToken = localStorage.getItem(AUTH_TOKEN_KEY);
    const storedUser = storedToken ? getUserFromToken(storedToken) : null;
    if (storedToken && storedUser) {
      setToken(storedToken);
      setUser(storedUser);
    } else {
      localStorage.removeItem(AUTH_TOKEN_KEY);
      setToken(null);
      setUser(null);
    }

    setLoading(false);
  }, [configLoading]);

  const login = useCallback(async (username: string, password: string) => {
    const response = await fetch(`${config.apiUrl}/api/v1/auth/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username, password }),
    });

    if (!response.ok) {
      const error = await response.json().catch(() => ({ error: { message: "Login failed" } }));
      throw new Error(error.error?.message || "Login failed");
    }

    const data = await response.json();
    const newToken = data.token as string;
    const newUser = getUserFromToken(newToken);
    if (!newUser) {
      throw new Error("Login response did not include a valid admin session");
    }

    localStorage.setItem(AUTH_TOKEN_KEY, newToken);
    setToken(newToken);
    setUser(newUser);
  }, [config.apiUrl]);

  const logout = useCallback(() => {
    localStorage.removeItem(AUTH_TOKEN_KEY);
    setToken(null);
    setUser(null);
  }, []);

  return (
    <AuthContext.Provider value={{ user, token, loading, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  const context = useContext(AuthContext);
  if (!context) {
    throw new Error("useAuth must be used within an AuthProvider");
  }
  return context;
}
