from pathlib import Path
import re

ROOT = Path("/workspaces/2022_SONICOM-HRTF-DATASET")

MIS_MEDIDAS = {
    "X1": 155.0,
    "X2": 240.0,
    "X3": 230.0,
    "X6": 370.0,
    "X7": 130.0,
    "X12": 380.0,
    "D1": 18.0,
    "D2": 12.0,
    "D3": 25.0,
    "D4": 22.0,
    "D5": 70.0,
    "D6": 42.0,
    "D7": 3.0,
    "D8": 12.0,
    "theta1": 0.0,
    "theta2": 20.0,
}

PESOS = {
    "X1": 0.5,
    "X2": 0.5,
    "X3": 0.5,
    "X6": 0.1,
    "X7": 0.1,
    "X12": 0.1,
    "D1": 1.5,
    "D2": 1.5,
    "D3": 1.0,
    "D4": 1.0,
    "D5": 2.0,
    "D6": 2.0,
    "D7": 1.0,
    "D8": 3.0,
    "theta1": 0.2,
    "theta2": 0.2,
}


def distancia_ponderada(a, b):
    total = 0.0
    for clave in PESOS:
        if clave in a and clave in b:
            diff = a[clave] - b[clave]
            total += (diff * PESOS[clave]) ** 2
    return total ** 0.5


def extraer_medidas_del_texto(texto):
    datos = {}
    matches = re.findall(r"(X\d+|D\d+)\s*[:=]\s*([-+]?\d*\.?\d+)", texto, flags=re.I)
    for clave, valor in matches:
        try:
            datos[clave] = float(valor)
        except Exception:
            pass
    return datos


def buscar_medidas_en_carpeta(subject_dir: Path):
    medidas = {}
    for archivo in subject_dir.rglob("*"):
        if not archivo.is_file():
            continue
        if archivo.suffix.lower() not in {".txt", ".csv", ".json", ".md"}:
            continue
        try:
            texto = archivo.read_text(encoding="utf-8", errors="ignore")
            medidas_archivo = extraer_medidas_del_texto(texto)
            if medidas_archivo:
                medidas.update(medidas_archivo)
        except Exception:
            pass
    return medidas


def buscar_soa_principal(subject_dir: Path):
    carpetas_prioridad = [
        "HRTF",
        "SYNTHETIC_HRTF",
        "SINTETIC_HRTF",
        "synthetic",
        "HRTF_SINTETICO",
    ]

    for nombre in carpetas_prioridad:
        carpeta = subject_dir / nombre
        if carpeta.exists():
            sofas = sorted(carpeta.rglob("*.sofa"))
            if sofas:
                return sofas[0]

    sofas = sorted(subject_dir.rglob("*.sofa"))
    if sofas:
        return sofas[0]

    return None


def listar_sujetos():
    if not ROOT.exists():
        print(f"No existe la ruta del dataset: {ROOT}")
        return []

    sujetos = []
    for item in sorted(ROOT.iterdir()):
        if not item.is_dir():
            continue
        if re.match(r"^P_\d+", item.name, re.I):
            medidas = buscar_medidas_en_carpeta(item)
            sofa = buscar_soa_principal(item)
            sujetos.append({
                "id": item.name,
                "ruta": item,
                "medidas": medidas,
                "sofa": sofa,
            })
    return sujetos


def main():
    sujetos = listar_sujetos()

    if not sujetos:
        print("No encontré carpetas tipo P_XXX en la ruta configurada.")
        print("Ajustá ROOT en la parte superior del archivo.")
        return

    ranking = []
    for sujeto in sujetos:
        if len(sujeto["medidas"]) < 3:
            continue
        dist = distancia_ponderada(MIS_MEDIDAS, sujeto["medidas"])
        ranking.append((dist, sujeto["id"], sujeto["ruta"], sujeto["sofa"]))

    if not ranking:
        print("Encontré sujetos, pero no tenían medidas útiles para comparar.")
        print("Revisá si los archivos tienen nombres tipo X6=370 o D8: 12.")
        return

    ranking.sort(key=lambda x: x[0])

    print("Top candidatos por medidas antropométricas:")
    for i, (dist, sid, ruta, sofa) in enumerate(ranking[:10], start=1):
        print(f"{i}. {sid} | distancia={dist:.4f}")
        if sofa:
            print(f"   sofa: {sofa}")
        else:
            print("   sofa: no encontrado")

    mejor_dist, mejor_id, mejor_ruta, mejor_sofa = ranking[0]
    print("\nMás parecido:")
    print(f"- sujeto: {mejor_id}")
    print(f"- distancia: {mejor_dist:.4f}")
    if mejor_sofa:
        print(f"- sofa: {mejor_sofa}")
    else:
        print("- sofa: no se encontró ningún .sofa en ese sujeto")


if __name__ == "__main__":
    main()
