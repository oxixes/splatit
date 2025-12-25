import { Loader2 } from "lucide-react";

export function ServerStatusLoading() {
  return (
    <div className="min-h-screen bg-slate-950 flex items-center justify-center p-4">
      <div className="text-center">
        <div className="flex justify-center mb-6">
          <Loader2 className="h-16 w-16 text-slate-100 animate-spin" />
        </div>
        <h2 className="text-2xl font-semibold text-slate-100 mb-2">
          Connecting to Server...
        </h2>
        <p className="text-slate-400">
          Please wait while we verify the server status
        </p>
      </div>
    </div>
  );
}

