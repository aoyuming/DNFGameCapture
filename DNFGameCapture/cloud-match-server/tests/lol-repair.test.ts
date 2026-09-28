import {test,expect} from 'vitest';
import {LOL_REPAIR,canRepairLol} from '../src/lol-repair.js';
test('only the 28 known LOL source revisions can be replaced with their exact clean revisions',()=>{
 expect(Object.keys(LOL_REPAIR)).toHaveLength(28);
 expect(new Set(Object.values(LOL_REPAIR).map(v=>v.clean)).size).toBe(7);
 for(const [key,e] of Object.entries(LOL_REPAIR)){
  expect(canRepairLol('lol-announcer',key,e.old,e.clean)).toBe(true);
  expect(canRepairLol('doubao-meihuo',key,e.old,e.clean)).toBe(false);
  expect(canRepairLol('lol-announcer',key,'f'.repeat(64),e.clean)).toBe(false);
  expect(canRepairLol('lol-announcer',key,e.old,'f'.repeat(64))).toBe(false);
 }
 expect(canRepairLol('lol-announcer','unknown','x','y')).toBe(false);
});
