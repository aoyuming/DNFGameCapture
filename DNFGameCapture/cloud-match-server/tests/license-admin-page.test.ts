import { Script } from 'node:vm';
import { describe, expect, test } from 'vitest';
import { ADMIN_PAGE_CSS } from '../src/admin-page.js';
import * as page from '../src/license-admin-page.js';

describe('license admin page contract', () => {
  test('composes the shared stylesheet with license-scoped rules', () => {
    const css = (page as unknown as { LICENSE_ADMIN_CSS?: string }).LICENSE_ADMIN_CSS;
    expect(css).toBeTypeOf('string');
    expect(css?.startsWith(ADMIN_PAGE_CSS)).toBe(true);
    expect(css?.slice(ADMIN_PAGE_CSS.length)).toContain('.license-admin');
  });

  test('uses native presets and bounded quantities, without timestamp entry', () => {
    const html = page.buildLicenseAdminPage('token');
    expect(html).toMatch(/<select[^>]+id="license-preset"/);
    expect(html).toMatch(/<input[^>]+id="license-quantity"[^>]+type="number"[^>]+min="1"[^>]+max="200"/);
    expect(html).not.toMatch(/license-expiry|datetime-local|Unix/);
    expect(html).toContain('自定义卡密（仅单张）');
  });

  test('provides batch recovery, retry and accessible modal surfaces', () => {
    const html = page.buildLicenseAdminPage('token');
    for (const id of ['issued-keys', 'copy-all-keys', 'export-keys', 'retry-mutation', 'license-dialog', 'dialog-preview', 'audit-list']) {
      expect(html).toContain(`id="${id}"`);
    }
    expect(html).toMatch(/<dialog[^>]+aria-labelledby="dialog-title"/);
  });

  test('escapes CSRF attributes and uses external CSP-compatible bindings', () => {
    const html = page.buildLicenseAdminPage('\"><script>alert(1)</script>&\'');
    expect(html).toContain('&quot;&gt;&lt;script&gt;alert(1)&lt;/script&gt;&amp;&#39;');
    expect(html).not.toMatch(/\son\w+=|<style|<script(?! src=)/);
    expect(page.LICENSE_ADMIN_JS).not.toMatch(/\.innerHTML|\.outerHTML|\.on(?:click|submit|input)\s*=|\beval\(/);
    expect(() => new Script(page.LICENSE_ADMIN_JS)).not.toThrow();
  });

  test('keeps key recovery and writes on protected same-origin APIs', () => {
    expect(page.LICENSE_ADMIN_JS).toContain('x-dnf-admin-csrf');
    expect(page.LICENSE_ADMIN_JS).toContain('requestId');
    expect(page.LICENSE_ADMIN_JS).toContain('revision');
    expect(page.LICENSE_ADMIN_JS).not.toMatch(/localStorage|sessionStorage|https?:\/\//);
    expect(page.LICENSE_ADMIN_JS).toContain('broadcasterName');
    expect(page.buildLicenseAdminPage('token')).toContain('激活主播');
  });

  test('bounds stalled requests and names backend audit fields in Chinese', () => {
    expect(page.LICENSE_ADMIN_JS).toContain('AbortController');
    expect(page.LICENSE_ADMIN_JS).toContain('30000');
    for (const field of ['beforeExpiresAt', 'beforeDurationSeconds', 'beforeDeviceId', 'batchCount', 'firstActivation', 'recover_key']) {
      expect(page.LICENSE_ADMIN_JS).toContain(field + ':');
    }
  });
});
