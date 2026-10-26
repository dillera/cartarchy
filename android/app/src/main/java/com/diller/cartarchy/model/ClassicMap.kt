package com.diller.cartarchy.model

object ClassicMap {
    private fun territory(
        id: String,
        name: String,
        continent: Continent,
        x: Double,
        y: Double,
        vararg neighbors: String,
    ) = TerritoryDefinition(id, name, continent, x, y, neighbors.toList())

    val territories = listOf(
        territory("alaska", "Alaska", Continent.NORTH_AMERICA, .06, .22, "nwTerritory", "alberta", "kamchatka"),
        territory("nwTerritory", "N.W. Territory", Continent.NORTH_AMERICA, .15, .18, "alaska", "alberta", "ontario", "greenland"),
        territory("greenland", "Greenland", Continent.NORTH_AMERICA, .27, .14, "nwTerritory", "ontario", "quebec", "iceland"),
        territory("alberta", "Alberta", Continent.NORTH_AMERICA, .13, .29, "alaska", "nwTerritory", "ontario", "westernUS"),
        territory("ontario", "Ontario", Continent.NORTH_AMERICA, .20, .28, "nwTerritory", "greenland", "quebec", "easternUS", "westernUS", "alberta"),
        territory("quebec", "Quebec", Continent.NORTH_AMERICA, .27, .27, "greenland", "ontario", "easternUS"),
        territory("westernUS", "Western U.S.", Continent.NORTH_AMERICA, .15, .39, "alberta", "ontario", "easternUS", "centralAmerica"),
        territory("easternUS", "Eastern U.S.", Continent.NORTH_AMERICA, .23, .39, "ontario", "quebec", "westernUS", "centralAmerica"),
        territory("centralAmerica", "Central America", Continent.NORTH_AMERICA, .19, .50, "westernUS", "easternUS", "venezuela"),
        territory("venezuela", "Venezuela", Continent.SOUTH_AMERICA, .27, .57, "centralAmerica", "peru", "brazil"),
        territory("peru", "Peru", Continent.SOUTH_AMERICA, .29, .69, "venezuela", "brazil", "argentina"),
        territory("brazil", "Brazil", Continent.SOUTH_AMERICA, .36, .64, "venezuela", "peru", "argentina", "northAfrica"),
        territory("argentina", "Argentina", Continent.SOUTH_AMERICA, .32, .82, "peru", "brazil"),
        territory("iceland", "Iceland", Continent.EUROPE, .42, .20, "greenland", "greatBritain", "scandinavia"),
        territory("greatBritain", "Great Britain", Continent.EUROPE, .43, .31, "iceland", "scandinavia", "northernEurope", "westernEurope"),
        territory("scandinavia", "Scandinavia", Continent.EUROPE, .51, .21, "iceland", "greatBritain", "northernEurope", "ukraine"),
        territory("ukraine", "Ukraine", Continent.EUROPE, .59, .29, "scandinavia", "northernEurope", "southernEurope", "ural", "afghanistan", "middleEast"),
        territory("northernEurope", "Northern Europe", Continent.EUROPE, .51, .34, "greatBritain", "scandinavia", "ukraine", "southernEurope", "westernEurope"),
        territory("westernEurope", "Western Europe", Continent.EUROPE, .44, .42, "greatBritain", "northernEurope", "southernEurope", "northAfrica"),
        territory("southernEurope", "Southern Europe", Continent.EUROPE, .52, .42, "westernEurope", "northernEurope", "ukraine", "middleEast", "egypt", "northAfrica"),
        territory("northAfrica", "North Africa", Continent.AFRICA, .46, .57, "brazil", "westernEurope", "southernEurope", "egypt", "eastAfrica", "congo"),
        territory("egypt", "Egypt", Continent.AFRICA, .53, .54, "northAfrica", "southernEurope", "middleEast", "eastAfrica"),
        territory("eastAfrica", "East Africa", Continent.AFRICA, .57, .64, "egypt", "northAfrica", "congo", "southAfrica", "madagascar", "middleEast"),
        territory("congo", "Congo", Continent.AFRICA, .50, .68, "northAfrica", "eastAfrica", "southAfrica"),
        territory("southAfrica", "South Africa", Continent.AFRICA, .52, .82, "congo", "eastAfrica", "madagascar"),
        territory("madagascar", "Madagascar", Continent.AFRICA, .61, .80, "southAfrica", "eastAfrica"),
        territory("ural", "Ural", Continent.ASIA, .66, .25, "ukraine", "siberia", "china", "afghanistan"),
        territory("siberia", "Siberia", Continent.ASIA, .73, .19, "ural", "yakutsk", "irkutsk", "mongolia", "china"),
        territory("yakutsk", "Yakutsk", Continent.ASIA, .82, .16, "siberia", "irkutsk", "kamchatka"),
        territory("kamchatka", "Kamchatka", Continent.ASIA, .91, .20, "yakutsk", "irkutsk", "mongolia", "japan", "alaska"),
        territory("irkutsk", "Irkutsk", Continent.ASIA, .80, .27, "siberia", "yakutsk", "kamchatka", "mongolia"),
        territory("mongolia", "Mongolia", Continent.ASIA, .80, .36, "siberia", "irkutsk", "kamchatka", "japan", "china"),
        territory("japan", "Japan", Continent.ASIA, .91, .38, "kamchatka", "mongolia"),
        territory("afghanistan", "Afghanistan", Continent.ASIA, .65, .39, "ukraine", "ural", "china", "india", "middleEast"),
        territory("middleEast", "Middle East", Continent.ASIA, .61, .50, "ukraine", "southernEurope", "egypt", "eastAfrica", "india", "afghanistan"),
        territory("india", "India", Continent.ASIA, .70, .52, "middleEast", "afghanistan", "china", "siam"),
        territory("china", "China", Continent.ASIA, .75, .42, "ural", "siberia", "mongolia", "afghanistan", "india", "siam"),
        territory("siam", "Siam", Continent.ASIA, .78, .55, "india", "china", "indonesia"),
        territory("indonesia", "Indonesia", Continent.AUSTRALIA, .79, .67, "siam", "newGuinea", "westernAustralia"),
        territory("newGuinea", "New Guinea", Continent.AUSTRALIA, .88, .68, "indonesia", "westernAustralia", "easternAustralia"),
        territory("westernAustralia", "Western Australia", Continent.AUSTRALIA, .82, .80, "indonesia", "newGuinea", "easternAustralia"),
        territory("easternAustralia", "Eastern Australia", Continent.AUSTRALIA, .90, .82, "newGuinea", "westernAustralia"),
    )

    val byId = territories.associateBy { it.id }
}
