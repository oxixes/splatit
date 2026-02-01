import type { Route } from "./+types/settings"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Button} from "~/components/ui/button";
import {Label} from "~/components/ui/label";
import {Switch} from "~/components/ui/switch";
import {Tabs, TabsContent, TabsList, TabsTrigger} from "~/components/ui/tabs";
import {AlertCircle, RefreshCw, Sparkles, Plus, Pencil, Trash2} from "lucide-react";
import {Link} from "react-router";
import { useState, useEffect } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import { getAgreements, saveAgreement, deleteAgreement } from "~/lib/agreements";
import { AgreementEditor } from "~/components/agreements/AgreementEditor";
import type { Agreement, AgreementsFilters, SortColumn } from "~/types/agreement";
import { AGREEMENT_TYPES } from "~/constants/agreement-types";
import { SortableHeader } from "~/components/ui/sortable-header";
import countriesLanguages from "~/data/countries_languages.json";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "~/components/ui/alert-dialog";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Settings - SplatIt Server" },
    { name: "description", content: "Configure server settings and preferences." },
  ]
}

export default function Settings() {
  const { config } = useAppConfig();

  const [agreements, setAgreements] = useState<Agreement[]>([]);
  const [pagination, setPagination] = useState({ totalItems: 0, totalPages: 0, currentPage: 1 });
  const [isLoading, setIsLoading] = useState(true);
  const [editorOpen, setEditorOpen] = useState(false);
  const [editingAgreement, setEditingAgreement] = useState<Agreement | null>(null);
  const [deleteDialogOpen, setDeleteDialogOpen] = useState(false);
  const [agreementToDelete, setAgreementToDelete] = useState<Agreement | null>(null);

  // Filters and sorting
  const [filters, setFilters] = useState<AgreementsFilters>({
    page: 0,
    pageSize: 10,
    sort: "type_asc"
  });

  useEffect(() => {
    loadAgreements();
  }, [filters]);

  const loadAgreements = async () => {
    try {
      setIsLoading(true);
      const response = await getAgreements(config, filters);
      setAgreements(response.agreements);
      setPagination(response.pagination);
    } catch (error) {
      console.error("Error loading agreements:", error);
    } finally {
      setIsLoading(false);
    }
  };

  const handleAddAgreement = () => {
    setEditingAgreement(null);
    setEditorOpen(true);
  };

  const handleEditAgreement = (agreement: Agreement) => {
    setEditingAgreement(agreement);
    setEditorOpen(true);
  };

  const handleSaveAgreement = async (agreement: Agreement) => {
    await saveAgreement(config, agreement);
    await loadAgreements();
  };

  const handleDeleteClick = (agreement: Agreement) => {
    setAgreementToDelete(agreement);
    setDeleteDialogOpen(true);
  };

  const handleDeleteConfirm = async () => {
    if (!agreementToDelete) return;

    try {
      await deleteAgreement(config, {
        type: agreementToDelete.type,
        version: agreementToDelete.version,
        country: agreementToDelete.country,
        language: agreementToDelete.language,
      });
      await loadAgreements();
    } catch (error) {
      console.error("Error deleting agreement:", error);
      alert("Failed to delete agreement");
    } finally {
      setDeleteDialogOpen(false);
      setAgreementToDelete(null);
    }
  };

  const getAgreementTypeLabel = (type: string) => {
    const agreementType = AGREEMENT_TYPES.find(t => t.value === type);
    return agreementType ? agreementType.label : type;
  };

  const getCountryName = (countryCode: string) => {
    const countryData = countriesLanguages.countries[countryCode as keyof typeof countriesLanguages.countries];
    return countryData ? countryData.name : countryCode;
  };

  const getLanguageName = (countryCode: string, languageCode: string) => {
    const countryData = countriesLanguages.countries[countryCode as keyof typeof countriesLanguages.countries];
    if (countryData) {
      const langData = countryData.languages[languageCode as keyof typeof countryData.languages];
      return langData ? (langData as { native: string; english: string }).english : languageCode;
    }
    return languageCode;
  };

  const handleSort = (key: string) => {
    const column = key as SortColumn;
    const currentSort = filters.sort;
    let newSort: typeof filters.sort = `${column}_desc`;
    if (currentSort?.startsWith(column)) {
      newSort = currentSort.endsWith("_asc") ? `${column}_desc` : `${column}_asc`;
    }
    setFilters({ ...filters, sort: newSort, page: 0 });
  };

  const handlePageChange = (newPage: number) => {
    setFilters({ ...filters, page: newPage });
  };


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
                              <Button onClick={handleAddAgreement}>
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
                              <div className="overflow-x-auto">
                                  <table className="w-full">
                                      <thead>
                                          <tr className="border-b">
                                              <SortableHeader label="Type" sortKey="type" currentSort={filters.sort} onSort={handleSort} />
                                              <SortableHeader label="Country" sortKey="country" currentSort={filters.sort} onSort={handleSort} />
                                              <SortableHeader label="Language" sortKey="language" currentSort={filters.sort} onSort={handleSort} />
                                              <SortableHeader label="Version" sortKey="version" currentSort={filters.sort} onSort={handleSort} />
                                              <th className="text-left py-2 px-2 text-sm font-medium">Actions</th>
                                          </tr>
                                      </thead>
                                      <tbody>
                                          {isLoading ? (
                                              <tr>
                                                  <td colSpan={5} className="text-center py-8 text-muted-foreground">
                                                      Loading agreements...
                                                  </td>
                                              </tr>
                                          ) : agreements.length === 0 ? (
                                              <tr>
                                                  <td colSpan={5} className="text-center py-8 text-muted-foreground">
                                                      <div>No agreements configured</div>
                                                      <div className="text-sm mt-2">Click "Add Agreement" to create your first EULA and Privacy Policy</div>
                                                  </td>
                                              </tr>
                                      ) : (
                                          agreements.map((agreement) => (
                                              <tr
                                                  key={`${agreement.type}-${agreement.version}-${agreement.country}-${agreement.language}`}
                                                  className="border-b last:border-0 hover:bg-muted/50 cursor-pointer"
                                                  onClick={() => handleEditAgreement(agreement)}
                                              >
                                                  <td className="py-3 px-2">
                                                      <div className="font-medium">{getAgreementTypeLabel(agreement.type)}</div>
                                                  </td>
                                                  <td className="py-3 px-2">
                                                      <div className="text-sm">{getCountryName(agreement.country)}</div>
                                                      <div className="text-xs text-muted-foreground">{agreement.country}</div>
                                                  </td>
                                                  <td className="py-3 px-2">
                                                      <div className="text-sm">{getLanguageName(agreement.country, agreement.language)} ({agreement.language.toUpperCase()})</div>
                                                      <div className="text-xs text-muted-foreground">{agreement.languageName}</div>
                                                  </td>
                                                  <td className="py-3 px-2 font-medium">{agreement.version}</td>
                                                  <td className="py-3 px-2">
                                                      <div className="flex gap-2">
                                                          <Button
                                                              variant="ghost"
                                                              size="sm"
                                                              onClick={(e) => {
                                                                  e.stopPropagation();
                                                                  handleEditAgreement(agreement);
                                                              }}
                                                          >
                                                              <Pencil className="h-4 w-4" />
                                                          </Button>
                                                          <Button
                                                              variant="ghost"
                                                              size="sm"
                                                              onClick={(e) => {
                                                                  e.stopPropagation();
                                                                  handleDeleteClick(agreement);
                                                              }}
                                                          >
                                                              <Trash2 className="h-4 w-4 text-destructive" />
                                                          </Button>
                                                      </div>
                                                  </td>
                                              </tr>
                                          ))
                                      )}
                                      </tbody>
                                  </table>
                              </div>
                          </div>

                          {/* Pagination Controls */}
                          {pagination.totalPages > 1 && (
                              <div className="flex items-center justify-between pt-4 border-t">
                                  <div className="text-sm text-muted-foreground">
                                      Showing {pagination.currentPage * (filters.pageSize || 10) + 1} to{" "}
                                      {Math.min((pagination.currentPage + 1) * (filters.pageSize || 10), pagination.totalItems)} of{" "}
                                      {pagination.totalItems} agreements
                                  </div>
                                  <div className="flex items-center gap-2">
                                      <Button
                                          variant="outline"
                                          size="sm"
                                          onClick={() => handlePageChange(pagination.currentPage - 1)}
                                          disabled={pagination.currentPage === 0}
                                      >
                                          Previous
                                      </Button>
                                      <div className="text-sm">
                                          Page {pagination.currentPage + 1} of {pagination.totalPages}
                                      </div>
                                      <Button
                                          variant="outline"
                                          size="sm"
                                          onClick={() => handlePageChange(pagination.currentPage + 1)}
                                          disabled={pagination.currentPage === pagination.totalPages - 1}
                                      >
                                          Next
                                      </Button>
                                  </div>
                              </div>
                          )}
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
                      </CardContent>
                  </Card>

                  {/* Agreement Editor Modal */}
                  <AgreementEditor
                      open={editorOpen}
                      onOpenChange={setEditorOpen}
                      agreement={editingAgreement}
                      onSave={handleSaveAgreement}
                  />

                  {/* Delete Confirmation Dialog */}
                  <AlertDialog open={deleteDialogOpen} onOpenChange={setDeleteDialogOpen}>
                      <AlertDialogContent>
                          <AlertDialogHeader>
                              <AlertDialogTitle>Delete Agreement</AlertDialogTitle>
                              <AlertDialogDescription>
                                  Are you sure you want to delete this agreement?
                                  {agreementToDelete && (
                                      <div className="mt-2 font-medium">
                                          {getAgreementTypeLabel(agreementToDelete.type)} - {agreementToDelete.country}/{agreementToDelete.language.toUpperCase()} v{agreementToDelete.version}
                                      </div>
                                  )}
                              </AlertDialogDescription>
                          </AlertDialogHeader>
                          <AlertDialogFooter>
                              <AlertDialogCancel>Cancel</AlertDialogCancel>
                              <AlertDialogAction onClick={handleDeleteConfirm} className="bg-destructive text-destructive-foreground">
                                  Delete
                              </AlertDialogAction>
                          </AlertDialogFooter>
                      </AlertDialogContent>
                  </AlertDialog>
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

