import type { Route } from './+types/home';
import { HomeLayout } from 'fumadocs-ui/layouts/home';
import { Link } from 'react-router';
import { baseOptions } from '@/lib/layout.shared';

export function meta({}: Route.MetaArgs) {
  return [
    { title: 'MeshTools Docs' },
    { name: 'description', content: 'User guide for importing, selecting, editing, and saving work in MeshTools.' },
  ];
}

export default function Home() {
  return (
    <HomeLayout {...baseOptions()}>
      <div className="p-4 flex flex-col items-center justify-center text-center flex-1">
        <h1 className="text-xl font-bold mb-2">MeshTools Documentation</h1>
        <p className="text-fd-muted-foreground mb-4">
          User guide for the MeshTools desktop editor, covering file workflows, viewport controls, selections, editing tools, and project saves.
        </p>
        <Link
          className="text-sm bg-fd-primary text-fd-primary-foreground rounded-full font-medium px-4 py-2.5"
          to="/docs"
        >
          Read The Guide
        </Link>
      </div>
    </HomeLayout>
  );
}
