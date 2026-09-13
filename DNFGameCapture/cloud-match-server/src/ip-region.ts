import { isIP } from 'node:net';
import geoip, { type Lookup } from 'geoip-lite';

export type GeoIpLookup = (ipAddress: string) => Lookup | null;

const countryNames = (() => {
  try {
    return new Intl.DisplayNames(['zh-CN'], { type: 'region' });
  } catch {
    return null;
  }
})();

export function normalizeIpAddress(value: string): string | null {
  let normalized = value.trim();
  if (normalized.startsWith('[') && normalized.endsWith(']')) {
    normalized = normalized.slice(1, -1);
  }
  const zone = normalized.indexOf('%');
  if (zone >= 0) normalized = normalized.slice(0, zone);
  const mapped = /^::ffff:(\d{1,3}(?:\.\d{1,3}){3})$/iu.exec(normalized);
  if (mapped && isIP(mapped[1]) === 4) return mapped[1];
  return isIP(normalized) ? normalized.toLocaleLowerCase() : null;
}

function isPrivateIpv4(ipAddress: string): boolean {
  const [first, second] = ipAddress.split('.').map(Number);
  return first === 0 || first === 10 || first === 127 || first >= 224 ||
    (first === 100 && second >= 64 && second <= 127) ||
    (first === 169 && second === 254) ||
    (first === 172 && second >= 16 && second <= 31) ||
    (first === 192 && second === 0) ||
    (first === 192 && second === 168) ||
    (first === 192 && second === 0 && ipAddress.startsWith('192.0.0.')) ||
    (first === 192 && second === 0 && ipAddress.startsWith('192.0.2.')) ||
    (first === 198 && (second === 18 || second === 19)) ||
    (first === 198 && second === 51 && ipAddress.startsWith('198.51.100.')) ||
    (first === 203 && second === 0 && ipAddress.startsWith('203.0.113.'));
}

export function isPublicIpAddress(value: string): boolean {
  const ipAddress = normalizeIpAddress(value);
  if (!ipAddress) return false;
  const version = isIP(ipAddress);
  if (version === 4) return !isPrivateIpv4(ipAddress);
  return ipAddress !== '::' && ipAddress !== '::1' &&
    !ipAddress.startsWith('fc') && !ipAddress.startsWith('fd') &&
    !/^fe[89ab]/u.test(ipAddress) && !ipAddress.startsWith('ff') &&
    !ipAddress.startsWith('2001:db8:');
}

export function resolveIpRegion(
  value: string,
  lookup: GeoIpLookup = geoip.lookup,
): string {
  const ipAddress = normalizeIpAddress(value);
  if (!ipAddress) return '未知地区';
  if (!isPublicIpAddress(ipAddress)) return '内网';
  try {
    const result = lookup(ipAddress);
    if (!result) return '未知地区';
    const country = result.country
      ? countryNames?.of(result.country.toUpperCase()) ?? result.country.toUpperCase()
      : '';
    const parts = [country, result.region, result.city]
      .map(item => item.trim())
      .filter((item, index, values) => item && values.indexOf(item) === index);
    return parts.join(' · ') || '未知地区';
  } catch {
    return '未知地区';
  }
}
