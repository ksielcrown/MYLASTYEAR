# Mapeo del reporte Intelisis "Inventario Detallado por Artículo – Refacciones"

Documento de análisis de la muestra `Inventario_detallado_por_clave_de_articulo_segunda_semana_de_sep.xlsx`
(corte 18-Sep-2026, 07:12 pm, sucursal Mérida, almacén RS1) y propuesta de desarrollo con
**Google Sheets + Apps Script** para control de inventario y sugerencia de compra.

> El repositorio es público: aquí solo se documenta la **estructura** del reporte y conteos.
> No se deben subir los `.xlsx` reales ni montos de inventario/costos al repo.

---

## 1. Resumen ejecutivo

| Concepto | Valor en la muestra |
|---|---|
| Formato | `.xlsx`, **1 hoja**, rango `A1:Q1038`, 17 columnas |
| Tipo de reporte | Foto (snapshot) de existencias + historia de ventas de 7 meses |
| Artículos | **1,029** (sin claves duplicadas) |
| Almacenes | 1 (`RS1`) |
| Fórmulas / tablas / filtros | Ninguno (solo valores con formato de número) |
| Usuario que exportó | `BYDMREF02` (propiedad del archivo) |
| Artículos con existencia 0 | **No se incluyen** (el reporte solo lista artículos con existencia) |

**Qué sí trae:** existencia, costo promedio, costo total, ubicación, fecha de última entrada y
salida, ventas por mes (mes en curso + 6 anteriores), promedio de venta y clasificación de rotación.

**Qué no trae:** unidad de medida, proveedor, familia/línea/modelo, precio de venta, mínimos/máximos,
pedidos en tránsito, apartados, tiempo de entrega y artículos agotados.

---

## 2. Anatomía del archivo (fila por fila)

| Fila | Contenido | Ejemplo | Uso en el ETL |
|---|---|---|---|
| 1 | `A1` título del reporte, `B1` sucursal | `Inventario Detallado por Articulo - Refacciones` / `Mérida` | Validar que es el reporte correcto; guardar sucursal |
| 2 | `A2` **fecha y hora de corte** (fecha real de Excel, formato `dd/mm/yyyy hh:mm:ss AM/PM`) | `18/09/2026 07:12:09 PM` | **Clave**: define a qué meses corresponden las columnas de ventas |
| 3 | `A3` rango de artículos, `B3` rango de almacenes | `Rango de Articulos Del 24121800 Al YUAN2DO` / `Rango de Almacenes Del RS1 Al RS1` | Guardar en bitácora; validar que siempre se corre con el mismo rango |
| 4 | `B4` leyenda de meses | `Mes --------- Historia de Ventas ----------` | Ignorar |
| 5 | **Encabezados** | `No. Parte`, `Descripción`, … `Cl` | Validar nombres y orden |
| 6 | Inicio de grupo por almacén: `A6 = "Almacen:"`, `B6 = "RS1"` | | Si el rango incluye varios almacenes habrá varios bloques |
| 7 – 1035 | **Datos**: una fila por artículo | | Se procesan |
| 1036 | `B = "Subtotal:"`, `C` = suma de existencias, `D` = suma de costo total | `6327.97` / `4,5xx,xxx.xx` | **Ojo:** los totales están corridos de columna (C y D, no D y F). Sirven como *checksum* |
| 1037 | `B = "Total:"` (mismos valores) | | Ignorar / checksum |
| 1038 | Pie: `A` fecha en texto, `B` página | `Viernes, 18 de Septiembre de 2026 (07:12 pm)` / `Página 1` | Ignorar |

Detalles de la hoja:
- El nombre de la hoja es `Inventario detallado por clave ` (con **espacio al final**, cortado a 31
  caracteres). **No buscar la hoja por nombre**: usar la primera hoja.
- No hay filas combinadas, paneles inmovilizados ni autofiltro.
- Los artículos vienen ordenados alfabéticamente como texto, excepto el primero (`24121800`), que
  aparece fuera de orden.
- El *checksum* no cuadra exacto con la suma de filas (existencias: 6,327.97 contra 6,327.70 sumando
  filas; el costo total también difiere por unos pesos). Probablemente Intelisis suma con más decimales
  de los que exporta. Se recomienda validar con **tolerancia** (±0.1 %).

---

## 3. Diccionario de columnas

| Col | Encabezado | Significado | Tipo real en el archivo | Observaciones | Transformación | Campo destino |
|---|---|---|---|---|---|---|
| A | `No. Parte` | Clave del artículo | Texto (1,025) **y número** (4: `24121800`, `20730424`, `62015`, `62402`) | 1,018 con patrón BYD `NNNNNNNN-NN`; 4 solo dígitos; 7 alfanuméricas (`PAQUETE051`, `BYD-M9`, `TACAFILABYD00`, `5935055T`, …) | `String(v).trim()` **siempre como texto** | `sku` |
| B | `Descripción` | Nombre del artículo | Texto | Mezcla inglés/español y mayúsculas/minúsculas; espacios dobles; algunos prefijos `[MT-18]`; menciona modelos (HAN 29, SEAL 17, KING 8, YUAN 5, SHARK 4, …) | `trim` + colapsar espacios. Opcional: extraer modelo por palabras clave | `descripcion` |
| C | `Ubica` | Ubicación física | Texto; `0` numérico cuando no tiene | 813 con patrón `Zona+Rack.Nivel` (`C01.18`), zonas A–J; **134 vacías/0**; ~75 sin nivel (`D03`); errores de captura: `CO1.9` (letra O), `E01,2` (coma), `D01-5` / `C01-8` / `H01-3` / `J02-1` (guion), `ALC` | `0`→vacío; `,` y `-`→`.`; `O`→`0` después de la zona | `ubica` |
| D | `Exist` | Existencia en unidad de inventario | Número | Nunca 0. Entera en 1,017; **decimal en 12** (aceites, anticongelante, refrigerante → probablemente litros) con basura de punto flotante (`3.99999999999906`) | `round(v, 2)` | `exist` |
| E | `C. Prom` | Costo promedio unitario (MXN) | Número, formato moneda | Rango aprox. $1.48 – $179,155.67 | Número | `c_prom` |
| F | `C. Total` | Existencia × costo promedio | Número, formato moneda | Cuadra con `D×E` (difiere centavos en 47 filas por redondeo de `C. Prom`) | Usar este valor para valuar el inventario | `c_total` |
| G | `Ult. Entr` | Fecha de la última entrada | **Texto** `dd-Mmm-yy` con meses en español (`27-Ago-26`, `19-Dic-25`, `02-Ene-26`) | Rango 03-Jul-24 a 18-Sep-26; nunca vacía | Parsear a fecha | `ult_entr` |
| H | `Ult. Sal` | Fecha de la última salida | **Texto**, mismo formato | **Vacía en 202 artículos** (sin salida registrada) | Parsear; vacío → `null` | `ult_sal` |
| I | `Vta` | Promedio mensual de venta | **Mixto**: < 1 viene como **texto** (`'0.14'` … `'0.86'`, 447 filas); 0 y ≥ 1 como número | Es exactamente `suma(J:P)/7` (verificado en 100 % de filas). **Incluye el mes en curso incompleto → subestima** | **No usar**; se recalcula | — |
| J | `Sep` | Unidades vendidas en el **mes del corte (parcial)** | Número entero | Solo cubre del 1 al 18 del mes | Marcar como parcial | `hist_ventas` |
| K–P | `Ago` … `Mar` | Unidades vendidas en los 6 meses anteriores (completos) | Número entero | **Encabezados relativos**: en octubre dirán `Oct … Abr`. **Negativos** en 6 artículos (devoluciones: `-2` en Ago después de venta en Jul) | *Unpivot* a (sku, año-mes, unidades) | `hist_ventas` |
| Q | `Cl` | Clasificación de rotación de Intelisis | Texto: `A B C D E F N` | Ver sección 4 | Guardar como referencia | `cl_intelisis` |

### Ejemplo de fila normalizada

Entrada (fila 8 del Excel):

```
10090840-00 | PLACA DE CLIP | C01.18 | 21 | (costo) | (costo) | 07-Sep-26 | 01-Jul-26 | 2.86 | 0 | 0 | 10 | 0 | 10 | 0 | 0 | D
```

Salida en `MAESTRO` / `STG_EXISTENCIAS`:

```
almacen=RS1  sku=10090840-00  descripcion="PLACA DE CLIP"  ubica=C01.18  exist=21
ult_entr=2026-09-07  ult_sal=2026-07-01  cl_intelisis=D
```

Salida en `HIST_VENTAS` (7 filas, una por mes):

```
RS1 | 10090840-00 | 2026-09 | 0  | parcial (18/30 días)
RS1 | 10090840-00 | 2026-08 | 0
RS1 | 10090840-00 | 2026-07 | 10
RS1 | 10090840-00 | 2026-06 | 0
RS1 | 10090840-00 | 2026-05 | 10
RS1 | 10090840-00 | 2026-04 | 0
RS1 | 10090840-00 | 2026-03 | 0
```

---

## 4. Clasificación `Cl` (inferida de los datos, **por confirmar**)

| Clase | Artículos | % del valor del inventario | Meses con venta (de 7) | Interpretación probable |
|---|---|---|---|---|
| **A** | 67 | 12.9 % | 5 – 7 | Alta rotación |
| **B** | 159 | 15.5 % | 3 – 7 | Rotación media |
| **C** | 300 | 34.8 % | 1 – 7 | Baja rotación |
| **D** | 182 | 15.6 % | 0 – 4 | Muy baja rotación; 38 sin salida registrada |
| **E** | 137 | 8.3 % | 0 – 1 | Sin movimiento / obsoleto; 69 sin salida registrada |
| **F** | 14 | 1.9 % | 0 – 1 | Entradas recientes (jun–sep 2026) y casi sin venta. Son piezas de colisión/carrocería (parabrisas, espejo, manijas, unidad multimedia): **parece pedido especial para un cliente** |
| **N** | 170 | 11.0 % | 0 – 5 | **Nuevos**: todas sus entradas son de abr-2026 en adelante; 101 aún sin venta |

- Las clases se traslapan en "meses con venta" (hay B con 7 meses y C con 7 meses), así que
  Intelisis probablemente las calcula con **otra ventana** (¿12 meses?) o por cantidad. Hay que confirmar el criterio.
- Propuesta: el sistema calcula **su propia** clasificación (ABC por valor consumido + XYZ por
  variabilidad) y conserva `Cl` de Intelisis como referencia y como filtro (por ejemplo, **F = no reponer automático**).

---

## 5. Hallazgos de datos y de negocio

### 5.1 Calidad de datos (el ETL debe tolerarlos)

| Hallazgo | Casos | Tratamiento |
|---|---|---|
| Clave como número en vez de texto | 4 | Forzar a texto |
| `Vta` como texto cuando < 1 | 447 | No se usa (se recalcula) |
| Fechas como texto con meses en español | todas | Parser propio (`Ene, Feb, Mar, Abr, May, Jun, Jul, Ago, Sep, Oct, Nov, Dic`) |
| Existencias con decimales y basura flotante | 12 | Redondear a 2 decimales |
| Meses negativos (devoluciones) | 6 | Neteo contra el mes anterior (ver 7.1) |
| Ubicación vacía o `0` | 134 | Reporte de calidad de datos |
| Ubicación con errores de captura | 7 | Normalizar y reportar |
| Totales de pie corridos de columna | 2 filas | Ignorar al leer; usar como checksum |
| Hoja con espacio al final del nombre | 1 | Leer la primera hoja |

### 5.2 Artículos con existencia 0 (no aparecen en el reporte)

El reporte **solo lista artículos con existencia**. Un artículo que se agota **desaparece** del
reporte, y justo esos son los más urgentes de comprar.

**Solución de diseño:** mantener un **maestro acumulado** de todos los artículos vistos alguna vez.
Si un artículo que estaba en la carga anterior **no aparece** en la nueva, se marca con
`exist = 0` y estatus **`AGOTADO`**, conservando su historia de ventas para seguir pronosticándolo.

Reglas de protección, porque la lógica depende de que el reporte cubra siempre lo mismo:
1. Validar que el **rango de artículos y almacenes** (fila 3) sea igual al de la carga anterior.
2. Si desaparece más de un umbral configurable (por ejemplo, 10 %) de los artículos, **no aplicar**
   los agotados y pedir confirmación (probablemente se corrió el reporte con otro filtro).
3. Un artículo que **nunca** ha tenido existencia no aparecerá nunca. Para esos (piezas nuevas que se
   quieren empezar a surtir) se puede agregar un renglón manual al maestro.

### 5.3 Señales de negocio (preliminares, solo con esta muestra)

- **Tendencia fuerte al alza**: las unidades vendidas pasan de 1,564 (mar) a 2,883 (ago), **+84 %**.
  Septiembre lleva 2,081 en 18 días (≈ 3,470 proyectado). Un promedio simple **subestimaría** la
  demanda: el modelo debe incluir tendencia.
- **363 artículos sin ninguna venta en 7 meses** (≈ 26 % del valor del inventario).
- **61 artículos clase A/B con menos de 1 mes de cobertura** (existencia / venta promedio mar–ago).
  Por ejemplo, el aceite 0W-20 vende ≈ 611 al mes y hay 13.75 en existencia.
- **420 artículos con demanda intermitente** (venden solo 1–3 de 7 meses): requieren métodos para
  demanda intermitente, no promedio.
- **53 artículos con más de 12 meses de cobertura** (sobreinventario).
- Fluidos (aceites, anticongelante) con existencia fraccionaria: la **unidad de venta no es la
  unidad de compra** (¿litro contra tambor o garrafa?). Hace falta el factor de conversión.

---

## 6. Qué falta y de dónde sacarlo

| Dato | Para qué | Fuente sugerida |
|---|---|---|
| Pedidos en tránsito / back orders | No volver a pedir lo que ya viene en camino | Reporte de compras pendientes de Intelisis, o captura en hoja `TRANSITO` |
| Apartados para órdenes de servicio | Existencia *disponible* real | Reporte de Intelisis, o captura manual |
| Tiempo de entrega por proveedor/origen | Punto de reorden y stock de seguridad | Hoja `PARAMETROS` (la llena el ingeniero) |
| Frecuencia de pedido (semanal/mensual) | Horizonte de cobertura | `CONFIG` / por proveedor |
| Unidad de compra, múltiplo y lote mínimo | Redondeo del pedido | `MAESTRO` (columnas editables) |
| Proveedor / origen (BYD, local, accesorios) | Agrupar el pedido | `MAESTRO` |
| Historia de más de 7 meses (ideal 24) | Estacionalidad y tendencia confiables | Correr el reporte con cortes pasados, o un reporte de ventas mensuales por artículo |
| Criterio de la clase `Cl` (sobre todo F y N) | Reglas de reposición | Confirmar con el ingeniero o en la configuración de Intelisis |
| Formato de pedido de la armadora (si existe portal) | Generar el archivo de pedido directo | Ingeniero |
| Ventas perdidas (cliente pidió y no había) | Demanda real de artículos agotados | Bitácora en mostrador, si existe |

---

## 7. Propuesta de desarrollo con Google Sheets + Apps Script

### 7.0 Arquitectura

```
                     ┌────────────── Google Drive ──────────────┐
Intelisis ─ xlsx ──► │ 01_Entrada  →  02_Procesados / 03_Errores │
 (o por correo)      └──────────────────────┬───────────────────┘
                                            │ trigger cada hora o botón de menú
                                            ▼
                ┌──────────── Apps Script ─────────────────────────────────┐
                │ ingesta → parser → validación → carga (upsert)           │
                │        → motor de pronóstico → motor de reposición       │
                └───────────────┬──────────────────────────────────────────┘
                                ▼
       Google Sheets: MAESTRO, HIST_VENTAS, HIST_EXISTENCIAS, CALC (datos)
                      SUGERENCIA, TABLERO, CALIDAD_DATOS (vistas para el ingeniero)
                                ▼
                 Exportar .xlsx / PDF de pedido por proveedor
```

### 7.1 Hojas del libro (modelo de datos)

**Técnicas (ocultas y protegidas):**

| Hoja | Llave | Columnas | Tamaño estimado |
|---|---|---|---|
| `CONFIG` | parámetro | IDs de carpetas, nivel de servicio por clase, periodo de revisión (7/30 días), lead time por defecto, umbral de "desaparecidos", correos para notificaciones | ~30 filas |
| `CARGAS` (bitácora) | `id_carga` | archivo, fecha_corte, almacén(es), rango, filas_leídas, artículos, nuevos, agotados, rechazados, checksum_ok, estado, mensaje, usuario, timestamp | 1 fila por carga |
| `STG_EXISTENCIAS` | almacén + sku | foto normalizada de la última carga (todas las columnas del reporte ya limpias) | ~1,050 |
| `HIST_EXISTENCIAS` | fecha_corte + almacén + sku | exist, c_prom, c_total | ~55 mil filas/año con carga semanal |
| `HIST_VENTAS` | almacén + sku + periodo (`YYYY-MM`) | unidades, es_parcial, dias_cubiertos, dias_mes, fecha_corte_origen, id_carga | ~1,050 × 24 meses ≈ 25 mil |
| `CALC` | almacén + sku | demanda pronosticada, método, desviación, tendencia, stock de seguridad, punto de reorden, máximo, disponible, cobertura en días, sugerido, clase ABC/XYZ, motivo | ~1,050 |
| `HIST_SUGERENCIAS` | fecha + sku | sugerido contra aprobado (auditoría y mejora del modelo) | crece por corrida |

**Regla de carga de `HIST_VENTAS`: gana la carga más reciente.** Cada semana el mes en curso crece
(parcial) y los meses cerrados pueden ajustarse por devoluciones tardías. Con eso, cargar el reporte
cada semana **va construyendo la historia** sin duplicar nada.

**Neteo de devoluciones:** si un mes queda negativo, se resta del mes anterior (mínimo 0) y el mes
negativo queda en 0. Ejemplo: `Jul = 3, Ago = -2` → `Jul = 1, Ago = 0`.

**Para el ingeniero:**

| Hoja | Contenido |
|---|---|
| `MAESTRO` | Un renglón por artículo. **Columnas del sistema** (las escribe el script): sku, descripción, ubicación, cl_intelisis, estatus (`ACTIVO` / `NUEVO` / `AGOTADO` / `SIN_MOVIMIENTO`), primera y última vez visto, existencia y costo actuales. **Columnas editables** (el script nunca las toca): proveedor, lead time, unidad de compra, factor, múltiplo, lote mínimo, nivel de servicio especial, mín/máx manual, `¿Reponer? (S/N)`, notas |
| `TRANSITO` | Pedidos en camino: sku, cantidad, fecha de pedido, fecha estimada, proveedor, estatus |
| `SUGERENCIA` | La vista de trabajo (ver 7.5) |
| `TABLERO` | Indicadores y gráficos |
| `CALIDAD_DATOS` | Anomalías de la última carga: ubicaciones vacías o mal capturadas, fechas inválidas, desaparecidos, checksum |

> El script localiza las columnas del `MAESTRO` **por nombre de encabezado**, no por posición.
> Así el ingeniero puede reordenar o agregar columnas sin romper nada.

### 7.2 Ingesta

1. **Entrada.**
   - **Principal:** carpeta de Drive `01_Entrada` con un *trigger* de tiempo cada hora que procesa
     cualquier `.xlsx` nuevo.
   - **Manual:** menú `📦 Inventario → Cargar reporte…` con un diálogo HTML para subir el archivo.
   - **Opcional:** si Intelisis puede enviar el reporte por correo programado, leer el adjunto de
     Gmail con una etiqueta (`GmailApp.search('label:intelisis-inventario is:unread has:attachment')`).
2. **Conversión xlsx → Google Sheet temporal** con el servicio avanzado de Drive:
   ```js
   const tmp = Drive.Files.create(
     { name: 'tmp_' + blob.getName(), mimeType: MimeType.GOOGLE_SHEETS, parents: [tmpFolderId] },
     blob
   );
   const values = SpreadsheetApp.openById(tmp.id).getSheets()[0].getDataRange().getValues();
   DriveApp.getFileById(tmp.id).setTrashed(true);
   ```
   Al convertir, Sheets puede interpretar algunos textos (`'0.14'`, `'07-Sep-26'`) según la
   configuración regional. **El parser acepta ambos casos** (texto o valor ya convertido).
3. **Parser** (función pura, sin llamadas a Sheets, para poder probarla fuera de Apps Script):
   ubica el encabezado, valida columnas, calcula periodos y convierte filas.
4. **Validaciones** (si alguna falla, el archivo va a `03_Errores` y se notifica):
   - Encabezados esperados y en orden.
   - Los meses del encabezado coinciden con los calculados desde la fecha de corte.
   - El mismo `fecha_corte + almacén` no se cargó antes (o se reemplaza, según `CONFIG`).
   - El rango (fila 3) es igual al histórico.
   - El checksum de existencias cuadra dentro de la tolerancia.
   - El porcentaje de desaparecidos está bajo el umbral.
5. **Carga.**
   - Upsert de `HIST_VENTAS`.
   - Foto en `HIST_EXISTENCIAS`.
   - Actualización del `MAESTRO`: nuevos → `NUEVO`; ausentes → `AGOTADO` con `exist = 0`.
6. **Recalcular** `CALC` → `SUGERENCIA` → `TABLERO`.
7. **Bitácora** en `CARGAS`, mover el archivo a `02_Procesados` y mandar un correo resumen.

Buenas prácticas en Apps Script:
- **Un solo** `getValues()` / `setValues()` por hoja. Para hacer upsert: leer toda la hoja, indexar en
  un `Map` por llave, modificar en memoria y reescribir. Con ~25 mil filas tarda segundos. Nunca
  escribir celda por celda.
- `LockService.getScriptLock()` para que no corran dos cargas al mismo tiempo.
- Zona horaria del proyecto: `America/Merida` en `appsscript.json` (evita fechas corridas un día).
- Límite de 6 minutos por ejecución: con ~1,000 artículos sobra. Si llegaran a ser decenas de miles,
  dividir el proceso con triggers encadenados.

### 7.3 Parser (probado contra la muestra: 1,029 artículos, 7,203 registros de venta, 0 rechazados)

```js
const MESES = ['Ene', 'Feb', 'Mar', 'Abr', 'May', 'Jun', 'Jul', 'Ago', 'Sep', 'Oct', 'Nov', 'Dic'];
const COLS_FIJAS = ['No. Parte', 'Descripción', 'Ubica', 'Exist', 'C. Prom', 'C. Total', 'Ult. Entr', 'Ult. Sal', 'Vta'];

/** '07-Sep-26' -> Date. Si Sheets ya lo convirtió a Date, lo regresa tal cual. */
function parseFechaIntelisis(v) {
  if (v instanceof Date) return v;
  const s = String(v ?? '').trim();
  if (!s) return null;
  const m = s.match(/^(\d{1,2})-([A-Za-z]{3})-(\d{2})$/);
  const mes = m ? MESES.indexOf(m[2]) : -1;
  if (mes < 0) throw new Error('Fecha no reconocida: ' + s);
  return new Date(2000 + Number(m[3]), mes, Number(m[1]));
}

/** Convierte a número: acepta 12, '0.14', '', null. */
function num(v) {
  if (typeof v === 'number') return v;
  const n = parseFloat(String(v ?? '').replace(/[$,\s]/g, ''));
  return isNaN(n) ? 0 : n;
}

/** 'CO1.9' -> 'C01.9', 'E01,2' -> 'E01.2', 'D01-5' -> 'D01.5', 0/'' -> ''. */
function normalizarUbica(v) {
  let s = String(v ?? '').trim().toUpperCase();
  if (s === '0') return '';
  s = s.replace(/[,\-]/g, '.');
  return s.replace(/^([A-Z])O(\d)/, '$10$2');
}

/**
 * values: resultado de getValues() de la hoja convertida.
 * Regresa { corte, almacenes, periodos, articulos[], ventas[], rechazados[] }.
 */
function parseReporteInventario(values) {
  const iHdr = values.findIndex(r => String(r[0] ?? '').trim() === 'No. Parte');
  if (iHdr < 0) throw new Error('No se encontró la fila de encabezados ("No. Parte").');
  const hdr = values[iHdr].map(h => String(h ?? '').trim());
  COLS_FIJAS.forEach((h, i) => {
    if (hdr[i] !== h) throw new Error(`Columna ${i + 1}: se esperaba "${h}" y llegó "${hdr[i]}".`);
  });
  if (hdr[16] !== 'Cl') throw new Error('Columna 17: se esperaba "Cl".');

  const corte = values[1][0];                       // A2 = fecha/hora de corte
  if (!(corte instanceof Date)) throw new Error('A2 no contiene la fecha de corte.');

  // J..P = mes del corte y 6 meses hacia atrás. Se valida contra el encabezado.
  const periodos = [];
  for (let k = 0; k < 7; k++) {
    const d = new Date(corte.getFullYear(), corte.getMonth() - k, 1);
    if (hdr[9 + k] !== MESES[d.getMonth()]) {
      throw new Error(`Columna ${10 + k}: se esperaba "${MESES[d.getMonth()]}" y llegó "${hdr[9 + k]}".`);
    }
    const diasMes = new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate();
    periodos.push({
      periodo: `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}`,
      parcial: k === 0,
      dias: k === 0 ? corte.getDate() : diasMes,
      diasMes,
    });
  }

  let almacen = null;
  const almacenes = new Set(), articulos = [], ventas = [], rechazados = [];
  for (let i = iHdr + 1; i < values.length; i++) {
    const r = values[i];
    const a = String(r[0] ?? '').trim(), b = String(r[1] ?? '').trim();
    if (a === 'Almacen:') { almacen = b; almacenes.add(b); continue; }
    if (b === 'Subtotal:' || b === 'Total:' || b.startsWith('Página')) continue;
    if (!a) continue;
    if (!almacen || typeof r[3] !== 'number') { rechazados.push({ fila: i + 1, motivo: 'Fila no reconocida', valor: a }); continue; }

    const sku = a;                                   // siempre texto
    articulos.push({
      almacen, sku,
      descripcion: b.replace(/\s+/g, ' '),
      ubica: normalizarUbica(r[2]),
      exist: Math.round(num(r[3]) * 100) / 100,
      cProm: num(r[4]),
      cTotal: num(r[5]),
      ultEntr: parseFechaIntelisis(r[6]),
      ultSal: parseFechaIntelisis(r[7]),
      clIntelisis: String(r[16] ?? '').trim(),
    });
    periodos.forEach((p, k) => ventas.push({ almacen, sku, ...p, unidades: num(r[9 + k]) }));
  }
  return { corte, almacenes: [...almacenes], periodos, articulos, ventas, rechazados };
}
```

### 7.4 Motor de pronóstico y reposición

**Serie de demanda por artículo:** meses cerrados de `HIST_VENTAS`, con devoluciones ya neteadas.
El **mes parcial no entra al ajuste del modelo**. Se muestra aparte como "ritmo del mes"
(`unidades / días_cubiertos × días_mes`), como señal temprana.

**Segmentación** (define el método de cada artículo; criterio de Syntetos-Boylan con ADI = intervalo
promedio entre meses con venta y CV² = variabilidad de los meses con venta):

| Segmento | Regla | Método | Reposición |
|---|---|---|---|
| Excluido | `¿Reponer? = N`, clase F (pedido especial) | — | No se sugiere |
| Sin movimiento | 0 ventas en los últimos N meses (configurable, p. ej. 6) | — | No reponer; candidato a liquidar |
| Nuevo / poca historia | Menos de 3 meses desde su primera entrada | Promedio desde su alta, o mín/máx manual | Revisión del ingeniero |
| Regular | ADI ≤ 1.32 y CV² ≤ 0.49 | **Holt con tendencia amortiguada** (captura el crecimiento) | Normal (fórmula abajo) |
| Intermitente / errática | ADI > 1.32 o CV² > 0.49 | **SBA** (Croston corregido) | Poisson (fórmula abajo) |

```js
/** Croston corregido (SBA). serie = unidades por mes, sin el mes parcial. */
function pronosticoSBA(serie, alpha = 0.1) {
  let z = null, p = null, q = 1;
  for (const y of serie) {
    if (y > 0) {
      if (z === null) { z = y; p = q; }
      else { z += alpha * (y - z); p += alpha * (q - p); }
      q = 1;
    } else q++;
  }
  return z === null ? 0 : (1 - alpha / 2) * z / p;
}

/** Holt con tendencia amortiguada: pronóstico del siguiente mes. */
function pronosticoHolt(serie, alpha = 0.3, beta = 0.1, phi = 0.9) {
  const n = serie.length;
  let nivel = serie[0], tend = n > 1 ? (serie[n - 1] - serie[0]) / (n - 1) : 0;
  for (let t = 1; t < n; t++) {
    const prev = nivel;
    nivel = alpha * serie[t] + (1 - alpha) * (nivel + phi * tend);
    tend = beta * (nivel - prev) + (1 - beta) * phi * tend;
  }
  return Math.max(0, nivel + phi * tend);
}

/** Menor S tal que P(Poisson(media) <= S) >= nivel. Para medias chicas (< ~30). */
function cuantilPoisson(media, nivel) {
  let s = 0, p = Math.exp(-media), acum = p;
  while (acum < nivel) { s++; p *= media / s; acum += p; }
  return s;
}
```

**Fórmula de reposición** (sirve igual para compra semanal o mensual; solo cambia `R`):

```
D̂        = demanda mensual pronosticada
H        = (LeadTime + R) / 30          ← horizonte en meses; R = 7 (semanal) o 30 (mensual)
Disponible = Exist + En tránsito − Apartados

Máximo (nivel objetivo):
  si D̂·H < 20 → cuantilPoisson(D̂·H, NS)                 ← piezas de 0-1-2 unidades
  si no       → D̂·H + z(NS) · σ_mensual · √H              ← σ = desviación de la demanda (o del error de pronóstico)

Punto de reorden = lo mismo, pero con H = LeadTime / 30
Sugerido = si Disponible ≤ Punto de reorden → Máximo − Disponible, si no → 0
           → convertir a unidad de compra (÷ factor), redondear hacia arriba al múltiplo, respetar el lote mínimo
```

- **Nivel de servicio (NS) por clase ABC**, configurable en `CONFIG`: A 97 %, B 95 %, C 90 %, D 80 %.
- `z(NS)`: Apps Script no trae `NORM.S.INV`; usar una tabla (0.80 → 0.84, 0.90 → 1.28,
  0.95 → 1.645, 0.97 → 1.88, 0.98 → 2.05, 0.99 → 2.33).
- **ABC propio** por valor consumido (unidades × costo, últimos 12 meses, corte 80/15/5 %) y
  **XYZ** por coeficiente de variación. La matriz ABC-XYZ va al tablero.
- **Cada fila de `CALC` lleva un `motivo`** legible (por ejemplo, "Intermitente: SBA 0.5/mes,
  Poisson 95 % → máx 2, disp 0 → pedir 2"). Así el ingeniero entiende y confía en la sugerencia.

**Backtesting:** al cerrar cada mes, comparar el pronóstico guardado contra la venta real y calcular
**WAPE** y **sesgo** por segmento. Se muestran en el tablero y sirven para ajustar `alpha`/`beta`.

### 7.5 Interfaz para el ingeniero

**Menú personalizado** (`onOpen`):

```
📦 Inventario
 ├─ Cargar reporte de Intelisis…     (diálogo para subir el xlsx)
 ├─ Procesar carpeta de entrada
 ├─ Recalcular sugerencia
 ├─ Generar pedido por proveedor     (PDF/XLSX por proveedor en Drive)
 ├─ Exportar tablero a Excel (.xlsx)
 └─ Ver bitácora de cargas
```

**Hoja `SUGERENCIA`** (la hoja de trabajo diaria):
- Columnas: `Semáforo | No. Parte | Descripción | Clase | Segmento | Exist | Tránsito | Disponible |
  Venta prom. | Pronóstico | Cobertura (días) | Tendencia (sparkline) | Punto de reorden | Máximo |
  Sugerido | Cantidad aprobada ✏️ | Aprobar ☑ | Proveedor | Costo pedido | Motivo`.
- Semáforo con formato condicional: 🔴 agotado o cobertura < lead time; 🟡 disponible < punto de
  reorden; 🟢 OK; 🔵 sobreinventario (> 12 meses).
- `=SPARKLINE()` con los últimos 12 meses por artículo.
- Casillas de verificación y validación de datos en "Cantidad aprobada".
- Rangos protegidos en todo lo que calcula el sistema.
- **Vistas de filtro** y *slicers* por clase, proveedor, zona de almacén y semáforo.

**Hoja `TABLERO`:**
- Indicadores: valor del inventario, número de agotados, número en riesgo, monto de la compra
  sugerida, % del inventario sin movimiento, WAPE del mes.
- Gráficos:
  - Ventas mensuales en unidades (muestra la tendencia).
  - Valor del inventario por clase.
  - Top 20 artículos en riesgo.
  - Top 20 artículos de sobreinventario.
- Construido con fórmulas `QUERY` sobre `CALC`, para que se actualice solo al recalcular.

**Exportar a Excel:** para quien prefiera Excel,
`UrlFetchApp.fetch('https://docs.google.com/spreadsheets/d/' + id + '/export?format=xlsx', { headers: { Authorization: 'Bearer ' + ScriptApp.getOAuthToken() } })`,
guardado en Drive o enviado por correo. El formato condicional se conserva; `SPARKLINE` no existe en
Excel (queda la celda vacía), así que conviene una versión de exportación sin esa columna.

**Estética:**
- Encabezados inmovilizados y colores sobrios (1 color de marca + grises).
- Encabezados de columnas editables en otro color, para que el ingeniero sepa dónde puede escribir.
- Hojas técnicas ocultas.
- Formato de moneda y de miles.

### 7.6 Automatizaciones (triggers)

| Trigger | Frecuencia | Acción |
|---|---|---|
| `procesarCarpetaEntrada` | Cada hora | Procesa los xlsx nuevos de `01_Entrada` |
| `recalcularYNotificar` | Lunes 7:00 am y día 1 de cada mes | Recalcula y manda un correo con resumen (agotados, en riesgo, monto sugerido) y liga al libro |
| `cerrarMes` | Día 2 de cada mes | Guarda el pronóstico del mes y calcula el error del mes anterior (backtesting) |

### 7.7 Estructura del proyecto en el repo (con `clasp`)

```
apps-script/
  appsscript.json          ← timeZone "America/Merida", servicio avanzado Drive v3
  00_config.gs             ← lectura de CONFIG, constantes
  10_ingesta.gs            ← carpeta de Drive / Gmail / subida manual, conversión xlsx
  20_parser_intelisis.gs   ← parseReporteInventario (función pura)
  30_carga.gs              ← upsert HIST_VENTAS, HIST_EXISTENCIAS, MAESTRO, agotados
  40_pronostico.gs         ← segmentación, SBA, Holt, ABC/XYZ (funciones puras)
  50_reposicion.gs         ← stock de seguridad, punto de reorden, sugerido
  60_ui.gs, upload.html    ← menú, diálogo de carga, formato de hojas
  70_exportar.gs           ← pedido por proveedor, exportar xlsx
  90_utils.gs              ← bitácora, lock, fechas
tests/                     ← pruebas en Node (Jest) de parser y pronóstico
  fixtures/                ← muestra ANONIMIZADA (sin costos reales)
docs/
```

Mantener el parser y el pronóstico como **funciones puras** (sin `SpreadsheetApp`) permite probarlos
en Node con `clasp` + Jest antes de subirlos a Apps Script.

---

## 8. Plan por fases

| Fase | Entregable | Incluye |
|---|---|---|
| **1. MVP** | Carga y visibilidad | Menú, carga manual, parser, `MAESTRO` + agotados, `HIST_VENTAS`, `CALIDAD_DATOS`, cobertura y semáforo, sugerido simple (Holt/promedio + lead time por defecto) |
| **2. Motor** | Sugerencia confiable | Segmentación, SBA, Poisson, ABC/XYZ, parámetros por proveedor, `TRANSITO`, aprobación, pedido por proveedor |
| **3. Automatización** | Operación sin intervención | Carpeta/correo automático, triggers, correo resumen, backtesting, tablero completo, exportar a xlsx, `HIST_SUGERENCIAS` |

---

## 9. Preguntas abiertas para el ingeniero

1. ¿Qué criterio usa Intelisis para la clase `Cl`? ¿Qué significan **F** y **N**?
2. ¿Los artículos F son pedidos especiales de cliente (no se reponen)?
3. Tiempos de entrega por origen (BYD, proveedores locales, accesorios) y frecuencia de pedido.
4. Unidad de compra de fluidos (aceite, anticongelante, refrigerante): ¿litro, garrafa, tambor?
5. ¿Hay reporte de compras pendientes / back orders y de apartados en Intelisis?
6. ¿Se puede correr este mismo reporte con **fecha de corte en el pasado** para reconstruir 24 meses de historia?
7. ¿Solo almacén RS1 o habrá más almacenes o sucursales?
8. ¿El pedido se captura en un portal de la armadora con formato específico?
9. ¿El rango de artículos (`24121800` a `YUAN2DO`) es siempre el mismo al generar el reporte?
