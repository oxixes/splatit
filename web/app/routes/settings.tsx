import type { Route } from "./+types/settings"
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from "~/components/ui/card";
import {Button} from "~/components/ui/button";
import {Label} from "~/components/ui/label";
import {Switch} from "~/components/ui/switch";
import {Tabs, TabsContent, TabsList, TabsTrigger} from "~/components/ui/tabs";
import {AlertCircle, RefreshCw, Sparkles, Plus, Pencil, Trash2, Check, Shuffle, BarChart3} from "lucide-react";
import {Link} from "react-router";
import { useState, useEffect } from "react";
import { useAppConfig } from "~/hooks/useAppConfig";
import { getAgreements, saveAgreement, deleteAgreement } from "~/lib/agreements";
import { getFestivals, switchActiveFestival, deleteFestival } from "~/lib/festivals";
import { getSecurityStatus, updateSecurityStatus } from "~/lib/security";
import type { SecurityStatus } from "~/lib/security";
import type { FestivalSummary } from "~/types/festival";
import { AgreementEditor } from "~/components/agreements/AgreementEditor";
import type { Agreement, AgreementsFilters, SortColumn } from "~/types/agreement";
import { AGREEMENT_TYPES } from "~/constants/agreement-types";
import { SortableHeader } from "~/components/ui/sortable-header";
import countriesLanguages from "~/data/countries_languages.json";
import {FestivalResultsDialog} from "~/components/festivals/FestivalResultsDialog";
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

  // Festival state
  const [festivals, setFestivals] = useState<FestivalSummary[]>([]);
  const [activeFestivalId, setActiveFestivalId] = useState<number>(0);
  const [festivalsLoading, setFestivalsLoading] = useState(true);
  const [festivalMsg, setFestivalMsg] = useState<string | null>(null);
  const [resultsDialogOpen, setResultsDialogOpen] = useState(false);
  const [resultsFestivalId, setResultsFestivalId] = useState(0);
  const [resultsTeamA, setResultsTeamA] = useState("");
  const [resultsTeamB, setResultsTeamB] = useState("");

  // Security state
  const [securitySettings, setSecuritySettings] = useState<SecurityStatus>({
    allowAccountCreation: true,
    allowRealWiiU: true,
    allowGeneratedWiiU: true,
    maintenanceMode: false,
  });
  const [securityLoading, setSecurityLoading] = useState(true);
  const [securityMsg, setSecurityMsg] = useState<string | null>(null);

  // Filters and sorting
  const [filters, setFilters] = useState<AgreementsFilters>({
    page: 0,
    pageSize: 10,
    sort: "type_asc"
  });

  useEffect(() => {
    loadAgreements();
  }, [filters]);

  useEffect(() => {
    loadFestivals();
  }, []);

  useEffect(() => {
    loadSecuritySettings();
  }, []);

  const loadFestivals = async () => {
    try {
      setFestivalsLoading(true);
      const data = await getFestivals(config);
      setFestivals(data.festivals || []);
      setActiveFestivalId(data.activeId);
    } catch (error) {
      console.error("Error loading festivals:", error);
    } finally {
      setFestivalsLoading(false);
    }
  };

  const loadSecuritySettings = async () => {
    try {
      setSecurityLoading(true);
      const data = await getSecurityStatus(config);
      setSecuritySettings(data);
    } catch (error) {
      console.error("Error loading security settings:", error);
    } finally {
      setSecurityLoading(false);
    }
  };

  const handleSaveSecuritySettings = async () => {
    try {
      await updateSecurityStatus(config, securitySettings);
      setSecurityMsg("Security settings saved successfully");
      setTimeout(() => setSecurityMsg(null), 3000);
    } catch (error) {
      console.error("Error saving security settings:", error);
      alert("Failed to save security settings");
    }
  };

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

  const handleSwitchFestival = async (id: number) => {
    try {
      await switchActiveFestival(config, id);
      setActiveFestivalId(id);
      setFestivalMsg(`Switched to festival ${id}`);
    } catch (error) {
      console.error("Error switching festival:", error);
      alert("Failed to switch active festival");
    }
  };

  const handleOpenResults = (festival: FestivalSummary) => {
    setResultsFestivalId(festival.id);
    setResultsTeamA(festival.teamAName);
    setResultsTeamB(festival.teamBName);
    setResultsDialogOpen(true);
  };

  const handleDeleteFestival = async (id: number) => {
    try {
      await deleteFestival(config, id);
      await loadFestivals();
      setFestivalMsg(`Festival ${id} deleted`);
    } catch (error) {
      console.error("Error deleting festival:", error);
      alert("Failed to delete festival");
    }
  };

  const getTeamNames = (f: FestivalSummary): string => {
    return `${f.teamAName} vs ${f.teamBName}`;
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
                              <div className="flex gap-2">
                                  <Link to="/settings/splatfest/new">
                                      <Button>
                                          <Sparkles className="mr-2 h-4 w-4" />
                                          Create Splatfest
                                      </Button>
                                  </Link>
                                  <Link to="/settings/map-rotation">
                                      <Button variant="outline">
                                          <Shuffle className="mr-2 h-4 w-4" />
                                          Map Rotation
                                      </Button>
                                  </Link>
                              </div>
                          </div>
                      </CardHeader>
                      <CardContent className="space-y-4">
                          {festivalMsg && (
                              <div className="rounded-lg border border-green-500/50 bg-green-500/5 p-4">
                                  <p className="text-sm text-green-500">{festivalMsg}</p>
                              </div>
                          )}

                          <div className="space-y-2">
                              {festivalsLoading ? (
                                  <div className="text-center py-8 text-muted-foreground">
                                      Loading festivals...
                                  </div>
                              ) : festivals.length === 0 ? (
                                  <div className="text-center py-8 text-muted-foreground">
                                      <Sparkles className="h-12 w-12 mx-auto mb-4 opacity-50" />
                                      <p className="text-lg font-medium">No Splatfests Created</p>
                                      <p className="text-sm mt-2">Create your first Splatfest to enable the game</p>
                                  </div>
                              ) : (
                                  <div className="overflow-x-auto">
                                      <table className="w-full">
                                          <thead>
                                              <tr className="border-b">
                                                  <th className="text-left py-2 px-2 text-sm font-medium">ID</th>
                                                  <th className="text-left py-2 px-2 text-sm font-medium">Teams</th>
                                                  <th className="text-left py-2 px-2 text-sm font-medium">Status</th>
                                                  <th className="text-left py-2 px-2 text-sm font-medium">Actions</th>
                                              </tr>
                                          </thead>
                                          <tbody>
                                              {festivals.map((f) => (
                                                  <tr key={f.id} className="border-b last:border-0 hover:bg-muted/50">
                                                      <td className="py-3 px-2 font-medium">{f.id}</td>
                                                      <td className="py-3 px-2 text-sm">{getTeamNames(f)}</td>
                                                      <td className="py-3 px-2">
                                                          {f.active ? (
                                                              <span className="inline-flex items-center gap-1 text-green-600 text-sm font-medium">
                                                                  <Check className="h-3 w-3" /> Active
                                                              </span>
                                                          ) : (
                                                              <span className="text-muted-foreground text-sm">Inactive</span>
                                                          )}
                                                      </td>
                                                      <td className="py-3 px-2">
                                                          <div className="flex gap-1">
                                                              <Button
                                                                  variant="ghost"
                                                                  size="sm"
                                                                  onClick={() => handleOpenResults(f)}
                                                                  title="View results"
                                                              >
                                                                  <BarChart3 className="h-4 w-4" />
                                                              </Button>
                                                              {!f.active && (
                                                                  <Button
                                                                      variant="ghost"
                                                                      size="sm"
                                                                      onClick={() => handleSwitchFestival(f.id)}
                                                                      title="Set as active"
                                                                  >
                                                                      <Sparkles className="h-4 w-4" />
                                                                  </Button>
                                                              )}
                                                              <Link to={`/settings/splatfest/${f.id}`}>
                                                                  <Button variant="ghost" size="sm">
                                                                      <Pencil className="h-4 w-4" />
                                                                  </Button>
                                                              </Link>
                                                              {!f.active && (
                                                                  <Button
                                                                      variant="ghost"
                                                                      size="sm"
                                                                      onClick={() => handleDeleteFestival(f.id)}
                                                                  >
                                                                      <Trash2 className="h-4 w-4 text-destructive" />
                                                                  </Button>
                                                              )}
                                                          </div>
                                                      </td>
                                                  </tr>
                                              ))}
                                          </tbody>
                                      </table>
                                  </div>
                              )}
                          </div>
                      </CardContent>
                  </Card>

                  <FestivalResultsDialog
                      config={config}
                      festivalId={resultsFestivalId}
                      teamAName={resultsTeamA}
                      teamBName={resultsTeamB}
                      open={resultsDialogOpen}
                      onOpenChange={setResultsDialogOpen}
                  />
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
                                          Modifying the map rotation will reset the current rotation schedule.
                                          Players may need to reconnect to see the new rotation.
                                      </p>
                                  </div>
                              </div>
                          </div>
                          <Link to="/settings/map-rotation">
                              <Button variant="default">
                                  <RefreshCw className="mr-2 h-4 w-4" />
                                  Open Map Rotation Editor
                              </Button>
                          </Link>
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
                          {securityMsg && (
                              <div className="rounded-lg border border-green-500/50 bg-green-500/5 p-4">
                                  <p className="text-sm text-green-500">{securityMsg}</p>
                              </div>
                          )}
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Allow Account Creation</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Allow new accounts to be created
                                  </p>
                              </div>
                              <Switch
                                  checked={securitySettings.allowAccountCreation}
                                  onCheckedChange={(checked) =>
                                      setSecuritySettings({ ...securitySettings, allowAccountCreation: checked })
                                  }
                              />
                          </div>
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Allow Real Wii U Consoles</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Allow connections from official Wii U hardware
                                  </p>
                              </div>
                              <Switch
                                  checked={securitySettings.allowRealWiiU}
                                  onCheckedChange={(checked) =>
                                      setSecuritySettings({ ...securitySettings, allowRealWiiU: checked })
                                  }
                              />
                          </div>
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Allow CEMU Users</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Allow connections from CEMU emulator with generated certificates
                                  </p>
                              </div>
                              <Switch
                                  checked={securitySettings.allowGeneratedWiiU}
                                  onCheckedChange={(checked) =>
                                      setSecuritySettings({ ...securitySettings, allowGeneratedWiiU: checked })
                                  }
                              />
                          </div>
                          <Button onClick={handleSaveSecuritySettings} disabled={securityLoading}>
                              Save Security Settings
                          </Button>
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
                          {securityMsg && (
                              <div className="rounded-lg border border-green-500/50 bg-green-500/5 p-4">
                                  <p className="text-sm text-green-500">{securityMsg}</p>
                              </div>
                          )}
                          <div className="flex items-center justify-between">
                              <div className="space-y-0.5">
                                  <Label>Enable Maintenance Mode</Label>
                                  <p className="text-sm text-muted-foreground">
                                      Prevents players from connecting to all game servers
                                  </p>
                              </div>
                              <Switch
                                  checked={securitySettings.maintenanceMode}
                                  onCheckedChange={(checked) =>
                                      setSecuritySettings({ ...securitySettings, maintenanceMode: checked })
                                  }
                              />
                          </div>
                          <Button onClick={handleSaveSecuritySettings} disabled={securityLoading}>
                              Save Changes
                          </Button>
                      </CardContent>
                  </Card>
              </TabsContent>
          </Tabs>
      </div>
  )
}

