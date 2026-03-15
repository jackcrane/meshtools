import type { Config } from '@react-router/dev/config';
import { readdir } from 'node:fs/promises';
import { join, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createGetUrl, getSlugs } from 'fumadocs-core/source';

const getUrl = createGetUrl('/docs');
const docsDir = fileURLToPath(new URL('./content/docs/', import.meta.url));

async function getMdxEntries(
  dir: string,
  rootDir = dir,
): Promise<string[]> {
  const entries = await readdir(dir, { withFileTypes: true });
  const paths = await Promise.all(
    entries.map(async (entry) => {
      const fullPath = join(dir, entry.name);

      if (entry.isDirectory()) return getMdxEntries(fullPath, rootDir);
      if (!entry.isFile() || !entry.name.endsWith('.mdx')) return [];

      return [relative(rootDir, fullPath).split(sep).join('/')];
    }),
  );

  return paths.flat();
}

export default {
  ssr: false,
  future: {
    v8_middleware: true,
  },
  async prerender({ getStaticPaths }) {
    const paths: string[] = [];
    const excluded: string[] = [];

    for (const path of getStaticPaths()) {
      if (!excluded.includes(path)) paths.push(path);
    }

    for (const entry of await getMdxEntries(docsDir)) {
      const slugs = getSlugs(entry);
      paths.push(getUrl(slugs), `/llms.mdx/docs/${[...slugs, 'index.mdx'].join('/')}`);
    }

    return paths;
  },
} satisfies Config;
