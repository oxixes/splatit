import { Button } from "~/components/ui/button"

import type { Route } from "./+types/home"
import {Card, CardHeader, CardTitle} from "~/components/ui/card";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Lobbies" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

export default function Home() {
  return (
      <div className="flex w-full">
          <Card className="flex-1">
              <CardHeader>
                  <CardTitle>Lobbies</CardTitle>
              </CardHeader>
          </Card>
      </div>
  )
}