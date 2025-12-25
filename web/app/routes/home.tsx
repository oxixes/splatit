import { Button } from "~/components/ui/button"

import type { Route } from "./+types/home"
import {Card, CardContent, CardHeader, CardTitle} from "~/components/ui/card";

export function meta({}: Route.MetaArgs) {
  return [
    { title: "Home" },
    { name: "description", content: "SplatIt Splatoon server recreation admin UI." },
  ]
}

export default function Home() {
  return (
      <div className="grid grid-cols-12 gap-3">
          <h1 className="col-span-full scroll-m-20 text-4xl font-extrabold tracking-tight text-balance mb-5">Home</h1>
          <Card className="col-span-12 md:col-span-6 mb-5">
              <CardHeader>
                  <CardTitle>Active users</CardTitle>
              </CardHeader>
              <CardContent>
                  <span className="text-3xl">0</span>
              </CardContent>
          </Card>
          <Card className="col-span-12 md:col-span-6 mb-5">
              <CardHeader>
                  <CardTitle>Registered users</CardTitle>
              </CardHeader>
                <CardContent>
                    <span className="text-3xl">0</span>
                </CardContent>
          </Card>
      </div>
  )
}