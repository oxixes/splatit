import type { Route } from "./+types/settings"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Button} from "~/components/ui/button";
import {Label} from "~/components/ui/label";
import {Switch} from "~/components/ui/switch";
import {Tabs, TabsContent, TabsList, TabsTrigger} from "~/components/ui/tabs";
import {AlertCircle, RefreshCw, Sparkles, Plus} from "lucide-react";
import {Link} from "react-router";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Settings - SplatIt Server" },
    { name: "description", content: "Configure server settings and preferences." },
  ]
}

export default function Settings() {
  return (
      <div className="space-y-6">
          <div className="flex items-center justify-between">
              <div>
                  <h1 className="scroll-m-20 text-4xl font-extrabold tracking-tight">
                      Settings
                  </h1>
                  <p className="text-muted-foreground mt-2">
                      Configure your server settings
                  </p>
              </div>
          </div>

          <Tabs defaultValue="agreements" className="space-y-4">
              <TabsList>
                  <TabsTrigger value="agreements">Agreements</TabsTrigger>
                  <TabsTrigger value="splatfests">Splatfests</TabsTrigger>
                  <TabsTrigger value="boss">Map Rotation</TabsTrigger>
                  <TabsTrigger value="security">Security</TabsTrigger>
                  <TabsTrigger value="general">General</TabsTrigger>
              </TabsList>

              <TabsContent value="agreements" className="space-y-4">
                  {/* Agreements Management */}
                  <Card>
                      <CardHeader>
                          <div className="flex items-center justify-between">
                              <div>
                                  <CardTitle>User Agreements</CardTitle>
                                  <CardDescription>
                                      Manage EULAs and Privacy Policies by country and language
                                  </CardDescription>
                              </div>
                              <Button>
                                  <Plus className="mr-2 h-4 w-4" />
                                  Add Agreement
                              </Button>
                          </div>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="rounded-lg border border-blue-500/50 bg-blue-500/5 p-4">
                              <div className="flex items-start gap-3">
                                  <AlertCircle className="h-5 w-5 text-blue-500 mt-0.5" />
                                  <div className="space-y-1">
                                      <p className="text-sm font-medium text-blue-500">Agreement Structure</p>
                                      <p className="text-sm text-muted-foreground">
                                          Each agreement is specific to a <strong>type</strong> (EULA or Privacy Policy),
                                          <strong> country</strong>, and <strong>language</strong>.
                                          A default agreement is used if no specific combination is found.
                                      </p>
                                  </div>
                              </div>
                          </div>

                          <div className="space-y-2">
                              <div className="grid grid-cols-3 gap-4 font-medium text-sm border-b pb-2">
                                  <div>Type / Country / Language</div>
                                  <div>Version</div>
                                  <div>Actions</div>
                              </div>

                              {/* Placeholder for agreement list */}
                              <div className="text-center py-8 text-muted-foreground">
                                  <p>No agreements configured</p>
                                  <p className="text-sm mt-2">Click "Add Agreement" to create your first EULA or Privacy Policy</p>
                              </div>
                          </div>
                      </CardContent>
                  </Card>

                  {/* Default Agreement Info */}
                  <Card className="border-yellow-500/50 bg-yellow-500/5">
                      <CardHeader>
                          <CardTitle className="text-yellow-500">Default Agreement Behavior</CardTitle>
                      </CardHeader>
                      <CardContent className="text-sm space-y-2">
                          <p>• If no agreement exists for a specific country/language combination, a default message is shown</p>
                          <p>• Players will see: "Hey, if you are reading this, it means the server administrator has not set up the agreements..."</p>
                          <p>• It's recommended to create at least one default agreement for common languages</p>
                      </CardContent>
                  </Card>
              </TabsContent>

              <TabsContent value="splatfests" className="space-y-4">
                  {/* Splatfests Management */}
                  <Card>
                      <CardHeader>
                          <div className="flex items-center justify-between">
                              <div>
                                  <CardTitle>Splatfest Events</CardTitle>
                                  <CardDescription>
                                      Manage Splatfest events. One must always be active (even if in the past).
                                  </CardDescription>
                              </div>
                              <Link to="/settings/splatfest/new">
                                  <Button>
                                      <Sparkles className="mr-2 h-4 w-4" />
                                      Create Splatfest
                                  </Button>
                              </Link>
                          </div>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="rounded-lg border border-blue-500/50 bg-blue-500/5 p-4">
                              <div className="flex items-start gap-3">
                                  <Sparkles className="h-5 w-5 text-blue-500 mt-0.5" />
                                  <div className="space-y-1">
                                      <p className="text-sm font-medium text-blue-500">Splatfest Requirements</p>
                                      <p className="text-sm text-muted-foreground">
                                          At least one Splatfest must be marked as "In Use" at all times.
                                          The game requires festival data even if the festival has ended.
                                          Only one can be "In Use" at a time.
                                      </p>
                                  </div>
                              </div>
                          </div>

                          <div className="space-y-2">
                              <div className="grid grid-cols-5 gap-4 font-medium text-sm border-b pb-2">
                                  <div>Festival ID</div>
                                  <div>Teams</div>
                                  <div>Period</div>
                                  <div>Status</div>
                                  <div>Actions</div>
                              </div>

                              {/* Placeholder for splatfest list */}
                              <div className="text-center py-8 text-muted-foreground">
                                  <Sparkles className="h-12 w-12 mx-auto mb-4 opacity-50" />
                                  <p className="text-lg font-medium">No Splatfests Created</p>
                                  <p className="text-sm mt-2">Create your first Splatfest to enable the game</p>
                              </div>
                          </div>
                      </CardContent>
                  </Card>

                  {/* Splatfest Info */}
                  <Card className="border-blue-500/50 bg-blue-500/5">
                      <CardHeader>
                          <CardTitle className="text-blue-500">Festival Configuration</CardTitle>
                      </CardHeader>
                      <CardContent className="text-sm space-y-2">
                          <p>• <strong>Multi-language:</strong> Support for all 9 languages (EU: DE/EN/ES/FR/IT, JP, US: EN/ES/FR)</p>
                          <p>• <strong>Dialogue:</strong> Configure announcements, start, and result news with Callie & Marie</p>
                          <p>• <strong>Teams:</strong> Custom team names, colors, and short names per language</p>
                          <p>• <strong>Stages:</strong> Select 3 stages for the festival rotation</p>
                          <p>• <strong>Timing:</strong> Announcement, start, end, result, and bonus periods</p>
                      </CardContent>
                  </Card>
              </TabsContent>

              <TabsContent value="boss" className="space-y-4">
                  {/* Map Rotation */}
                  <Card>
                      <CardHeader>
                          <CardTitle>Map Rotation</CardTitle>
                          <CardDescription>Manage the hourly map rotation served by BOSS</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="rounded-lg border border-yellow-500/50 bg-yellow-500/5 p-4">
                              <div className="flex items-start gap-3">
                                  <AlertCircle className="h-5 w-5 text-yellow-500 mt-0.5" />
                                  <div className="space-y-1">
                                      <p className="text-sm font-medium text-yellow-500">Warning</p>
                                      <p className="text-sm text-muted-foreground">
                                          Regenerating the map rotation will reset the current rotation schedule.
                                          Players may need to reconnect to see the new rotation.
                                      </p>
                                  </div>
                              </div>
                          </div>
                          <div className="space-y-2">
                              <p className="text-sm text-muted-foreground">
                                  The map rotation is automatically generated and served to clients.
                                  Custom rotation configuration is not yet available.
                              </p>
                              <Button variant="destructive">
                                  <RefreshCw className="mr-2 h-4 w-4" />
                                  Regenerate Map Rotation
                              </Button>
                          </div>
                      </CardContent>
                  </Card>
              </TabsContent>

              <TabsContent value="security" className="space-y-4">
                  <Card>
                      <CardHeader>
                          <CardTitle>Client Authentication</CardTitle>
                          <CardDescription>Configure which client types can connect</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Allow Real Wii U Consoles</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Allow connections from official Wii U hardware
                                  </p>
                              </div>
                              <Switch defaultChecked />
                          </div>
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Allow CEMU Users</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Allow connections from CEMU emulator with generated certificates
                                  </p>
                              </div>
                              <Switch defaultChecked />
                          </div>
                          <Button>Save Security Settings</Button>
                      </CardContent>
                  </Card>
              </TabsContent>

              <TabsContent value="general" className="space-y-4">
                  {/* Maintenance Mode */}
                  <Card>
                      <CardHeader>
                          <CardTitle>Maintenance Mode</CardTitle>
                          <CardDescription>Temporarily disable server access for maintenance</CardDescription>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Enable Maintenance Mode</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Prevents players from connecting to all game servers
                                  </p>
                              </div>
                              <Switch />
                          </div>
                          <Button>Save Changes</Button>
                      </CardContent>
                  </Card>
              </TabsContent>
          </Tabs>
      </div>
  )
}

