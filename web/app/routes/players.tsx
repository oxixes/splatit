import type { Route } from "./+types/players"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Users, Download} from "lucide-react";
import {Button} from "~/components/ui/button";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Players - SplatIt Server" },
    { name: "description", content: "Manage player accounts." },
  ]
}

export default function Players() {
  return (
      <div className="space-y-6">
          <div className="flex items-center justify-between">
              <div>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">
                      Player Accounts
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      View and manage registered player accounts
                  </p>
              </div>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Total Accounts</CardTitle>
                      <Users className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">Registered accounts</p>
                  </CardContent>
              </Card>

              <Card>
                  <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                      <CardTitle className="text-sm font-medium">Active Now</CardTitle>
                      <Users className="h-4 w-4 text-muted-foreground" />
                  </CardHeader>
                  <CardContent>
                      <div className="text-2xl font-bold">0</div>
                      <p className="text-xs text-muted-foreground">Currently online</p>
                  </CardContent>
              </Card>
          </div>

          <Card>
              <CardHeader>
                  <CardTitle>Account List</CardTitle>
                  <CardDescription>All registered player accounts from Account Server</CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="text-center py-12 text-muted-foreground">
                      <Users className="h-12 w-12 mx-auto mb-4 opacity-50" />
                      <p className="text-lg font-medium">No accounts registered</p>
                      <p className="text-sm mt-2">
                          Player accounts will appear here when they register
                      </p>
                  </div>
              </CardContent>
          </Card>

          {/* CEMU File Generation */}
          <Card>
              <CardHeader>
                  <CardTitle>CEMU File Generation</CardTitle>
                  <CardDescription>
                      Generate CEMU certificates and keys for players. Data provided by Account Server.
                  </CardDescription>
              </CardHeader>
              <CardContent>
                  <div className="space-y-4">
                      <p className="text-sm text-muted-foreground">
                          Select a player account to generate their CEMU authentication files (certificates and keys) as a ZIP file.
                      </p>
                      <Button disabled>
                          <Download className="mr-2 h-4 w-4" />
                          Generate CEMU Files
                      </Button>
                      <p className="text-xs text-muted-foreground">
                          Select a player from the list above to enable this option
                      </p>
                  </div>
              </CardContent>
          </Card>
      </div>
  )
}

