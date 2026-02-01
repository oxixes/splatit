import { useEffect, useState } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "~/components/ui/card";
import { Users } from "lucide-react";
import { useAppConfig } from "~/hooks/useAppConfig";
import { listAccounts } from "~/lib/accounts";
import { ApiError } from "~/lib/api-client";

export function TotalAccountsCard() {
  const { config } = useAppConfig();
  const [total, setTotal] = useState<number>(0);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    const loadTotal = async () => {
      try {
        setLoading(true);
        // Get total without filters, just first page to get pagination info
        const res = await listAccounts(config, { page: 0, pageSize: 1 });
        setTotal(res.pagination.totalItems);
      } catch (e) {
        if (e instanceof ApiError) {
          console.error("Error loading total accounts:", e.message);
        } else {
          console.error("Error loading total accounts:", e);
        }
      } finally {
        setLoading(false);
      }
    };

    void loadTotal();
  }, [config]);

  return (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
        <CardTitle className="text-sm font-medium">Total Accounts</CardTitle>
        <Users className="h-4 w-4 text-muted-foreground" />
      </CardHeader>
      <CardContent>
        <div className="text-2xl font-bold">{loading ? "..." : total}</div>
        <p className="text-xs text-muted-foreground">Registered accounts</p>
      </CardContent>
    </Card>
  );
}
